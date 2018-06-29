// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <ddk/protocol/nand.h>
#include <ddktl/device.h>
#include <ddktl/protocol/bad-block.h>
#include <ddktl/protocol/nand.h>

#include <fbl/array.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <zircon/device/skip-block.h>
#include <zircon/types.h>

#include "logical-to-physical-map.h"

namespace nand {

// Device implementation for 
class SkipBlockDevice;
using DeviceType = ddk::Device<SkipBlockDevice, ddk::GetSizable, ddk::Ioctlable>;

class SkipBlockDevice : public DeviceType {
public:
    // Spawns device node based on parent node.
    static zx_status_t Create(zx_device_t* parent);

    zx_status_t Bind();

    // Device protocol implementation.
    zx_off_t DdkGetSize() { return GetBlockSize() * ltop_.LogicalBlockCount(); }
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);
    void DdkUnbind() { DdkRemove(); }
    void DdkRelease() { delete this; }

private:
    explicit SkipBlockDevice(zx_device_t* parent, nand_protocol_t nand_proto,
                             bad_block_protocol_t bad_block_proto)
        : DeviceType(parent), nand_proto_(nand_proto), bad_block_proto_(bad_block_proto),
          nand_(&nand_proto_), bad_block_(&bad_block_proto_) {
        nand_.Query(&nand_info_, &parent_op_size_);
    }

    DISALLOW_COPY_ASSIGN_AND_MOVE(SkipBlockDevice);

    uint64_t GetBlockSize() const { return nand_info_.pages_per_block * nand_info_.page_size; }

    // Helper to get bad block list in a more idiomatic container.
    zx_status_t GetBadBlockList(fbl::Array<uint32_t>* bad_block_list);
    // Helper to validate VMO received through IOCTL.
    zx_status_t ValidateVmo(const skip_block_rw_operation_t& op) const;

    // skip-block IOCTL implementation.
    zx_status_t GetPartitionInfo(skip_block_partition_info_t* info) const;
    zx_status_t Read(skip_block_rw_operation_t* info);
    zx_status_t Write(const skip_block_rw_operation_t& info);

    nand_protocol_t nand_proto_;
    bad_block_protocol_t bad_block_proto_;
    ddk::NandProtocolProxy nand_ __TA_GUARDED(lock_);
    ddk::BadBlockProtocolProxy bad_block_ __TA_GUARDED(lock_);
    LogicalToPhysicalMap ltop_ __TA_GUARDED(lock_);
    fbl::Mutex lock_;
    nand_info_t nand_info_;
    size_t parent_op_size_;
    // Operation buffer of size parent_op_size_.
    fbl::Array<uint8_t> nand_op_ __TA_GUARDED(lock_);
};

} // namespace nand
