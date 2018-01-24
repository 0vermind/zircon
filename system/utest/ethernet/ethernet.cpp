// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/protocol/ethernet.h>
#include <fbl/auto_call.h>
#include <fbl/intrusive_single_list.h>
#include <fbl/type_support.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <fdio/watcher.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/device/device.h>
#include <zircon/device/ethernet.h>
#include <zircon/device/ethertap.h>
#include <zircon/status.h>
#include <zircon/types.h>
#include <zx/fifo.h>
#include <zx/socket.h>
#include <zx/time.h>
#include <zx/vmar.h>
#include <zx/vmo.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Delay for data to work through the system. The test will pause this long, so it's best
// to keep it fairly short. If it's too short, the test will occasionally be flaky,
// especially on qemu.
#define PROPAGATE_MSEC (50)
#define PROPAGATE_TIME (zx::deadline_after(zx::msec(PROPAGATE_MSEC)))
// We expect something to happen prior to timeout, and the test will fail if it doesn't. So
// wait longer to further reduce the likelihood of test flakiness.
#define FAIL_TIMEOUT (zx::deadline_after(zx::msec(3 * PROPAGATE_MSEC)))

// Because of test flakiness if a previous test case's ethertap device isn't cleaned up, we put a
// delay at the end of each test to give devmgr time to clean up the ethertap devices.
#define ETHTEST_CLEANUP_DELAY zx::nanosleep(PROPAGATE_TIME)

namespace {

const char kEthernetDir[] = "/dev/class/ethernet";
const char kTapctl[] = "/dev/misc/tapctl";
const uint8_t kTapMac[] = { 0x12, 0x20, 0x30, 0x40, 0x50, 0x60 };

const char* mxstrerror(zx_status_t status) {
    return zx_status_get_string(status);
}

zx_status_t CreateEthertapWithOption(uint32_t mtu, const char* name, zx::socket* sock,
                                     uint32_t options) {
    if (sock == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    int ctlfd = open(kTapctl, O_RDONLY);
    if (ctlfd < 0) {
        fprintf(stderr, "could not open %s: %s\n", kTapctl, strerror(errno));
        return ZX_ERR_IO;
    }
    auto closer = fbl::MakeAutoCall([ctlfd]() { close(ctlfd); });

    ethertap_ioctl_config_t config = {};
    strlcpy(config.name, name, ETHERTAP_MAX_NAME_LEN);
    config.options = options;
    // Uncomment this to trace ETHERTAP events
    //config.options |= ETHERTAP_OPT_TRACE;
    config.mtu = mtu;
    memcpy(config.mac, kTapMac, 6);
    ssize_t rc = ioctl_ethertap_config(ctlfd, &config, sock->reset_and_get_address());
    if (rc < 0) {
        zx_status_t status = static_cast<zx_status_t>(rc);
        fprintf(stderr, "could not configure ethertap device: %s\n", mxstrerror(status));
        return status;
    }
    return ZX_OK;
}

zx_status_t CreateEthertap(uint32_t mtu, const char* name, zx::socket* sock) {
    return CreateEthertapWithOption(mtu, name, sock, 0);
}

zx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) return ZX_OK;
    if (!strcmp(fn, ".") || !strcmp(fn, "..")) return ZX_OK;

    int devfd = openat(dirfd, fn, O_RDONLY);
    if (devfd < 0) {
        return ZX_OK;
    }
    auto closer = fbl::MakeAutoCall([devfd]() { close(devfd); });

    // See if this device is our ethertap device
    eth_info_t info;
    ssize_t rc = ioctl_ethernet_get_info(devfd, &info);
    if (rc < 0) {
        zx_status_t status = static_cast<zx_status_t>(rc);
        fprintf(stderr, "could not get ethernet info for %s/%s: %s\n", kEthernetDir, fn,
                mxstrerror(status));
        // Return ZX_OK to keep watching for devices.
        return ZX_OK;
    }
    if (!(info.features & ETH_FEATURE_SYNTH)) {
        // Not a match, keep looking.
        return ZX_OK;
    }

    // Found it!
    // TODO(tkilbourn): this might not be the test device we created; need a robust way of getting
    // the name of the tap device to check. Note that ioctl_device_get_device_name just returns
    // "ethernet" since that's the child of the tap device that we've opened here.
    auto fd = reinterpret_cast<int*>(cookie);
    *fd = devfd;
    closer.cancel();
    return ZX_ERR_STOP;
}

zx_status_t OpenEthertapDev(int* fd) {
    if (fd == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    int ethdir = open(kEthernetDir, O_RDONLY);
    if (ethdir < 0) {
        fprintf(stderr, "could not open %s: %s\n", kEthernetDir, strerror(errno));
        return ZX_ERR_IO;
    }

    zx_status_t status = fdio_watch_directory(ethdir, WatchCb, zx_deadline_after(ZX_SEC(2)),
                                              reinterpret_cast<void*>(fd));
    if (status == ZX_ERR_STOP) {
        return ZX_OK;
    } else {
        return status;
    }
}

struct FifoEntry : public fbl::SinglyLinkedListable<fbl::unique_ptr<FifoEntry>> {
    eth_fifo_entry_t e;
};

class EthernetClient {
  public:
    explicit EthernetClient(int fd) : fd_(fd) {}
    ~EthernetClient() {
        if (mapped_ > 0) {
            zx::vmar::root_self().unmap(mapped_, vmo_size_);
        }
        close(fd_);
    }

    zx_status_t Register(const char* name, uint32_t nbufs, uint16_t bufsize) {
        ssize_t rc = ioctl_ethernet_set_client_name(fd_, name, strlen(name) + 1);
        if (rc < 0) {
            fprintf(stderr, "could not set client name to %s: %zd\n", name, rc);
            return static_cast<zx_status_t>(rc);
        }

        eth_fifos_t fifos;
        rc = ioctl_ethernet_get_fifos(fd_, &fifos);
        if (rc < 0) {
            fprintf(stderr, "could not get fifos: %zd\n", rc);
            return static_cast<zx_status_t>(rc);
        }

        tx_.reset(fifos.tx_fifo);
        rx_.reset(fifos.rx_fifo);
        tx_depth_ = fifos.tx_depth;
        rx_depth_ = fifos.rx_depth;

        nbufs_ = nbufs;
        bufsize_ = bufsize;

        vmo_size_ = 2 * nbufs_ * bufsize_;
        zx_status_t status = zx::vmo::create(vmo_size_, 0u, &buf_);
        if (status != ZX_OK) {
            fprintf(stderr, "could not create a vmo of size %" PRIu64 ": %s\n", vmo_size_,
                    mxstrerror(status));
            return status;
        }

        status = zx::vmar::root_self().map(0, buf_, 0, vmo_size_,
                                           ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE,
                                           &mapped_);
        if (status != ZX_OK) {
            fprintf(stderr, "failed to map vmo: %s\n", mxstrerror(status));
            return status;
        }

        zx::vmo buf_copy;
        status = buf_.duplicate(ZX_RIGHT_SAME_RIGHTS, &buf_copy);
        if (status != ZX_OK) {
            fprintf(stderr, "failed to duplicate vmo: %s\n", mxstrerror(status));
            return status;
        }

        zx_handle_t bufh = buf_copy.release();
        rc = ioctl_ethernet_set_iobuf(fd_, &bufh);
        if (rc < 0) {
            fprintf(stderr, "failed to set eth iobuf: %zd\n", rc);
            return static_cast<zx_status_t>(rc);
        }

        uint32_t idx = 0;
        for (; idx < nbufs; idx++) {
            eth_fifo_entry_t entry = {
                .offset = idx * bufsize_,
                .length = bufsize_,
                .flags = 0,
                .cookie = nullptr,
            };
            uint32_t actual;
            status = rx_.write(&entry, sizeof(entry), &actual);
            if (status != ZX_OK) {
                fprintf(stderr, "failed call to write(): %s\n", mxstrerror(status));
                return status;
            }
        }

        for (; idx < 2 * nbufs; idx++) {
            auto entry = fbl::unique_ptr<FifoEntry>(new FifoEntry);
            entry->e.offset = idx * bufsize_;
            entry->e.length = bufsize_;
            entry->e.flags = 0;
            entry->e.cookie = reinterpret_cast<uint8_t*>(mapped_) + entry->e.offset;
            tx_available_.push_front(fbl::move(entry));
        }

        return ZX_OK;
    }

    zx_status_t Start() {
        ssize_t rc = ioctl_ethernet_start(fd_);
        return rc < 0 ? static_cast<zx_status_t>(rc) : ZX_OK;
    }

    zx_status_t Stop() {
        ssize_t rc = ioctl_ethernet_stop(fd_);
        return rc < 0 ? static_cast<zx_status_t>(rc) : ZX_OK;
    }

    zx_status_t GetStatus(uint32_t* eth_status) {
        ssize_t rc = ioctl_ethernet_get_status(fd_, eth_status);
        return rc < 0 ? static_cast<zx_status_t>(rc) : ZX_OK;
    }

    zx_status_t SetPromisc(bool on) {
        ssize_t rc = ioctl_ethernet_set_promisc(fd_, &on);
        return rc < 0 ? static_cast<zx_status_t>(rc) : ZX_OK;
    }

    zx::fifo* tx_fifo() { return &tx_; }
    zx::fifo* rx_fifo() { return &rx_; }
    uint32_t tx_depth() { return tx_depth_; }
    uint32_t rx_depth() { return rx_depth_; }

    uint8_t* GetRxBuffer(uint32_t offset) {
        return reinterpret_cast<uint8_t*>(mapped_) + offset;
    }

    eth_fifo_entry_t* GetTxBuffer() {
        auto entry_ptr = tx_available_.pop_front();
        eth_fifo_entry_t* entry = nullptr;
        if (entry_ptr != nullptr) {
            entry = &entry_ptr->e;
            tx_pending_.push_front(fbl::move(entry_ptr));
        }
        return entry;
    }

    void ReturnTxBuffer(eth_fifo_entry_t* entry) {
        auto entry_ptr = tx_pending_.erase_if(
                [entry](const FifoEntry& tx_entry) { return tx_entry.e.cookie == entry->cookie; });
        if (entry_ptr != nullptr) {
            tx_available_.push_front(fbl::move(entry_ptr));
        }
    }

  private:
    int fd_;

    uint64_t vmo_size_ = 0;
    zx::vmo buf_;
    uintptr_t mapped_ = 0;
    uint32_t nbufs_ = 0;
    uint16_t bufsize_ = 0;

    zx::fifo tx_;
    zx::fifo rx_;
    uint32_t tx_depth_ = 0;
    uint32_t rx_depth_ = 0;

    using FifoEntryPtr = fbl::unique_ptr<FifoEntry>;
    fbl::SinglyLinkedList<FifoEntryPtr> tx_available_;
    fbl::SinglyLinkedList<FifoEntryPtr> tx_pending_;
};

}  // namespace

#define HEADER_SIZE (sizeof(ethertap_socket_header_t))
#define READBUF_SIZE (ETHERTAP_MAX_MTU + HEADER_SIZE)

static bool ExpectSockRead(zx::socket* sock, uint32_t type, size_t size, void* data,
                           const char* msg) {
    BEGIN_HELPER;
    zx_signals_t obs;
    uint8_t read_buf[READBUF_SIZE];
    // The socket should be readable
    EXPECT_EQ(ZX_OK, sock->wait_one(ZX_SOCKET_READABLE, FAIL_TIMEOUT, &obs), msg);
    ASSERT_TRUE(obs & ZX_SOCKET_READABLE, msg);

    // Read the data from the socket, which should match what was written to the fifo
    size_t actual_sz = 0;
    EXPECT_EQ(ZX_OK, sock->read(0u, static_cast<void*>(read_buf), READBUF_SIZE, &actual_sz), msg);
    ASSERT_EQ(size, actual_sz - HEADER_SIZE, msg);
    auto header = reinterpret_cast<ethertap_socket_header*>(read_buf);
    EXPECT_EQ(type, header->type, msg);
    if (size > 0) {
        ASSERT_NONNULL(data, msg);
        EXPECT_BYTES_EQ(static_cast<uint8_t*>(data), read_buf + HEADER_SIZE, size, msg);
    }
    END_HELPER;
}

static bool ExpectPacketRead(zx::socket* sock, size_t size, void* data, const char* msg) {
    return ExpectSockRead(sock, ETHERTAP_MSG_PACKET, size, data, msg);
}

static bool ExpectSetParamRead(zx::socket* sock, uint32_t param, int32_t value,
                               size_t data_length, uint8_t* data, const char* msg) {
    ethertap_setparam_report_t report = {};
    report.param = param;
    report.value = value;
    report.data_length = data_length;
    if (data_length > 0 && data != nullptr) {
        memcpy(report.data, data, data_length);
    }
    return ExpectSockRead(sock, ETHERTAP_MSG_PARAM_REPORT, sizeof(report), &report, msg);
}

static bool EthernetStartTest() {
    BEGIN_TEST;
    // Create the ethertap device
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertap(1500, __func__, &sock));

    // Open the ethernet device
    int devfd = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    // Set up an ethernet client
    EthernetClient client(devfd);
    ASSERT_EQ(ZX_OK, client.Register(__func__, 32, 2048));

    // Verify no signals asserted on the rx fifo
    zx_signals_t obs;
    client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, zx::time(), &obs);
    EXPECT_FALSE(obs & ETH_SIGNAL_STATUS);

    // Start the ethernet client
    EXPECT_EQ(ZX_OK, client.Start());

    // Default link status should be OFFLINE
    uint32_t eth_status = 0;
    EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(0, eth_status);

    // Set the link status to online and verify
    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, FAIL_TIMEOUT, &obs));
    EXPECT_TRUE(obs & ETH_SIGNAL_STATUS);

    EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(ETH_STATUS_ONLINE, eth_status);

    // Shutdown the ethernet client
    EXPECT_EQ(ZX_OK, client.Stop());

    // Clean up the ethertap device
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

static bool EthernetLinkStatusTest() {
    BEGIN_TEST;
    // Create the ethertap device
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertap(1500, __func__, &sock));

    // Set the link status to online
    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);
    // Sleep for just long enough to let the signal propagate
    zx::nanosleep(PROPAGATE_TIME);

    // Open the ethernet device
    int devfd = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    // Set up an ethernet client
    EthernetClient client(devfd);
    ASSERT_EQ(ZX_OK, client.Register(__func__, 32, 2048));

    // Start the ethernet client
    EXPECT_EQ(ZX_OK, client.Start());

    // Link status should be ONLINE since we set it before starting the client
    uint32_t eth_status = 0;
    EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(ETH_STATUS_ONLINE, eth_status);

    // Now the device goes offline
    sock.signal_peer(0, ETHERTAP_SIGNAL_OFFLINE);

    // Verify the link status
    zx_signals_t obs;
    EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ETH_SIGNAL_STATUS, FAIL_TIMEOUT, &obs));
    EXPECT_TRUE(obs & ETH_SIGNAL_STATUS);

    EXPECT_EQ(ZX_OK, client.GetStatus(&eth_status));
    EXPECT_EQ(0, eth_status);

    // Shutdown the ethernet client
    EXPECT_EQ(ZX_OK, client.Stop());

    // Clean up the ethertap device
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

static bool EthernetSetPromiscMultiClientTest() {
    BEGIN_TEST;
    // Create the ethertap device
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertapWithOption(1500, __func__, &sock, ETHERTAP_OPT_REPORT_PARAM));

    // Open the ethernet devices
    int devfdA = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfdA));
    ASSERT_GE(devfdA, 0);
    int devfdB = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfdB));
    ASSERT_GE(devfdB, 0);

    // Set up ethernet clients
    EthernetClient clientA(devfdA);
    ASSERT_EQ(ZX_OK, clientA.Register(__func__, 32, 2048));
    EthernetClient clientB(devfdB);
    ASSERT_EQ(ZX_OK, clientB.Register(__func__, 32, 2048));

    // Start the ethernet clients
    EXPECT_EQ(ZX_OK, clientA.Start());
    EXPECT_EQ(ZX_OK, clientB.Start());

    zx_signals_t obs;
    // Ensure sock is empty before starting test - should be unnecessary
    EXPECT_EQ(ZX_ERR_TIMED_OUT, sock.wait_one(ZX_SOCKET_CONTROL_READABLE, PROPAGATE_TIME, &obs));

    // This should send an ethertap_setparam_report up the control channel,
    // saying param ETHMAC_SETPARAM_PROMISC, value true.
    clientA.SetPromisc(true);

    ExpectSetParamRead(&sock, ETHMAC_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)");

    // None of these should cause a change in promisc commands to ethermac.
    clientA.SetPromisc(true); // It was already requested by A.
    clientB.SetPromisc(true);
    clientA.SetPromisc(false); // A should now not want it, but B still does.
    EXPECT_EQ(ZX_ERR_TIMED_OUT, sock.wait_one(ZX_SOCKET_CONTROL_READABLE, PROPAGATE_TIME, &obs));

    // After the next line, no one wants promisc, so I should get a command to turn it off.
    clientB.SetPromisc(false);
    ExpectSetParamRead(&sock, ETHMAC_SETPARAM_PROMISC, 0, 0, nullptr, "Promisc should be off (2)");

    // Shutdown the ethernet clients
    EXPECT_EQ(ZX_OK, clientA.Stop());
    EXPECT_EQ(ZX_OK, clientB.Stop());

    // Clean up the ethertap device
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

static bool EthernetSetPromiscClearOnCloseTest() {
    BEGIN_TEST;
    // Create the ethertap device
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertapWithOption(1500, __func__, &sock, ETHERTAP_OPT_REPORT_PARAM));

    // Open the ethernet device
    int devfd = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    // Set up ethernet client
    auto pClient = fbl::make_unique<EthernetClient>(devfd);
    ASSERT_EQ(ZX_OK, pClient->Register(__func__, 32, 2048));

    // Start the ethernet client
    EXPECT_EQ(ZX_OK, pClient->Start());

    zx_signals_t obs;
    // Ensure sock is empty before starting test - should be unnecessary
    EXPECT_EQ(ZX_ERR_TIMED_OUT, sock.wait_one(ZX_SOCKET_CONTROL_READABLE, PROPAGATE_TIME, &obs));

    // This should send an ethertap_setparam_report up the control channel,
    // saying param ETHMAC_SETPARAM_PROMISC, value true.
    pClient->SetPromisc(true);

    ExpectSetParamRead(&sock, ETHMAC_SETPARAM_PROMISC, 1, 0, nullptr, "Promisc on (1)");

    // Shutdown the ethernet client.
    EXPECT_EQ(ZX_OK, pClient->Stop());
    pClient.reset(); // This will free devfd

    // That should have caused promisc to turn off.
    ExpectSetParamRead(&sock, ETHMAC_SETPARAM_PROMISC, 0, 0, nullptr, "Closed: promisc off (2)");

    // Clean up the ethertap device.
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

static bool EthernetDataTest_Send() {
    BEGIN_TEST;
    // Set up the tap device and the ethernet client
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertap(1500, __func__, &sock));

    int devfd = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    EthernetClient client(devfd);
    ASSERT_EQ(ZX_OK, client.Register(__func__, 32, 2048));
    ASSERT_EQ(ZX_OK, client.Start());

    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    // Ensure that the fifo is writable
    zx_signals_t obs;
    EXPECT_EQ(ZX_OK, client.tx_fifo()->wait_one(ZX_FIFO_WRITABLE, zx::time(), &obs));
    ASSERT_TRUE(obs & ZX_FIFO_WRITABLE);

    // Grab an available TX fifo entry
    auto entry = client.GetTxBuffer();
    ASSERT_TRUE(entry != nullptr);

    // Populate some data
    uint8_t* buf = static_cast<uint8_t*>(entry->cookie);
    for (int i = 0; i < 32; i++) {
        buf[i] = static_cast<uint8_t>(i & 0xff);
    }
    entry->length = 32;

    // Write to the TX fifo
    uint32_t actual = 0;
    ASSERT_EQ(ZX_OK, client.tx_fifo()->write(entry, sizeof(eth_fifo_entry_t), &actual));
    EXPECT_EQ(1u, actual);

    ExpectPacketRead(&sock, 32, buf, "");

    // Now the TX completion entry should be available to read from the TX fifo
    EXPECT_EQ(ZX_OK, client.tx_fifo()->wait_one(ZX_FIFO_READABLE, FAIL_TIMEOUT, &obs));
    ASSERT_TRUE(obs & ZX_FIFO_READABLE);

    eth_fifo_entry_t return_entry;
    ASSERT_EQ(ZX_OK, client.tx_fifo()->read(&return_entry, sizeof(eth_fifo_entry_t), &actual));
    EXPECT_EQ(1u, actual);

    // Check the flags on the returned entry
    EXPECT_TRUE(return_entry.flags & ETH_FIFO_TX_OK);
    return_entry.flags = 0;

    // Verify the bytes from the rest of the entry match what we wrote
    auto expected_entry = reinterpret_cast<uint8_t*>(entry);
    auto actual_entry = reinterpret_cast<uint8_t*>(&return_entry);
    EXPECT_BYTES_EQ(expected_entry, actual_entry, sizeof(eth_fifo_entry_t), "");

    // Return the buffer to our client; the client destructor will make sure no TXs are still
    // pending at the end of te test.
    client.ReturnTxBuffer(&return_entry);

    // Shutdown the client and cleanup the tap device
    EXPECT_EQ(ZX_OK, client.Stop());
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

static bool EthernetDataTest_Recv() {
    BEGIN_TEST;
    // Set up the tap device and the ethernet client
    zx::socket sock;
    ASSERT_EQ(ZX_OK, CreateEthertap(1500, __func__, &sock));

    int devfd = -1;
    ASSERT_EQ(ZX_OK, OpenEthertapDev(&devfd));
    ASSERT_GE(devfd, 0);

    EthernetClient client(devfd);
    ASSERT_EQ(ZX_OK, client.Register(__func__, 32, 2048));
    ASSERT_EQ(ZX_OK, client.Start());

    sock.signal_peer(0, ETHERTAP_SIGNAL_ONLINE);

    // The socket should be writable
    zx_signals_t obs;
    EXPECT_EQ(ZX_OK, sock.wait_one(ZX_SOCKET_WRITABLE, zx::time(), &obs));
    ASSERT_TRUE(obs & ZX_SOCKET_WRITABLE);

    // Send a buffer through the socket
    uint8_t buf[32];
    for (int i = 0; i < 32; i++) {
        buf[i] = static_cast<uint8_t>(i & 0xff);
    }
    size_t actual = 0;
    EXPECT_EQ(ZX_OK, sock.write(0, static_cast<void*>(buf), 32, &actual));
    EXPECT_EQ(32, actual);

    // The fifo should be readable
    EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ZX_FIFO_READABLE, FAIL_TIMEOUT, &obs));
    ASSERT_TRUE(obs & ZX_FIFO_READABLE);

    // Read the RX fifo
    eth_fifo_entry_t entry;
    uint32_t actual_entries = 0;
    EXPECT_EQ(ZX_OK, client.rx_fifo()->read(&entry, sizeof(eth_fifo_entry_t), &actual_entries));
    EXPECT_EQ(1, actual_entries);

    // Check the bytes in the VMO compared to what we sent through the socket
    auto return_buf = client.GetRxBuffer(entry.offset);
    EXPECT_BYTES_EQ(buf, return_buf, entry.length, "");

    // RX fifo should be readable, and we can return the buffer to the driver
    EXPECT_EQ(ZX_OK, client.rx_fifo()->wait_one(ZX_FIFO_WRITABLE, zx::time(), &obs));
    ASSERT_TRUE(obs & ZX_FIFO_WRITABLE);

    entry.length = 2048;
    EXPECT_EQ(ZX_OK, client.rx_fifo()->write(&entry, sizeof(eth_fifo_entry_t), &actual_entries));
    EXPECT_EQ(1, actual_entries);

    // Shutdown the client and cleanup the tap device
    EXPECT_EQ(ZX_OK, client.Stop());
    sock.reset();

    ETHTEST_CLEANUP_DELAY;
    END_TEST;
}

BEGIN_TEST_CASE(EthernetSetupTests)
RUN_TEST_MEDIUM(EthernetStartTest)
RUN_TEST_MEDIUM(EthernetLinkStatusTest)
END_TEST_CASE(EthernetSetupTests)

BEGIN_TEST_CASE(EthernetConfigTests)
RUN_TEST_MEDIUM(EthernetSetPromiscMultiClientTest)
RUN_TEST_MEDIUM(EthernetSetPromiscClearOnCloseTest)
END_TEST_CASE(EthernetConfigTests)

BEGIN_TEST_CASE(EthernetDataTests)
RUN_TEST_MEDIUM(EthernetDataTest_Send)
RUN_TEST_MEDIUM(EthernetDataTest_Recv)
END_TEST_CASE(EthernetDataTests)

int main(int argc, char* argv[]) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
