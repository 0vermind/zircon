// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <launchpad/launchpad.h>
#include <tftp/tftp.h>
#include <zircon/boot/netboot.h>
#include <zircon/syscalls.h>

#include "netsvc.h"

#define SCRATCHSZ 2048

// Identifies what the file being streamed over TFTP should be
// used for.
typedef enum netfile_type {
    netboot, // A bootfs file
    paver,   // A disk image which should be paved to disk
} netfile_type_t;

typedef struct {
    bool is_write;
    char filename[PATH_MAX + 1];
    netfile_type_t type;
    union {
        nbfile* netboot_file;
        struct {
            int fd;
            zx_handle_t process;
        } paver;
    };
} file_info_t;

typedef struct {
    ip6_addr_t dest_addr;
    uint16_t dest_port;
    uint32_t timeout_ms;
} transport_info_t;

static char tftp_session_scratch[SCRATCHSZ];
char tftp_out_scratch[SCRATCHSZ];

static size_t last_msg_size = 0;
static tftp_session* session = NULL;
static file_info_t file_info;
static transport_info_t transport_info;

zx_time_t tftp_next_timeout = ZX_TIME_INFINITE;

void file_init(file_info_t* file_info) {
    file_info->is_write = true;
    file_info->filename[0] = '\0';
    file_info->netboot_file = NULL;
}

static ssize_t file_open_read(const char* filename, void* cookie) {
    file_info_t* file_info = cookie;
    file_info->is_write = false;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';
    size_t file_size;
    if (netfile_open(filename, O_RDONLY, &file_size) == 0) {
        return (ssize_t)file_size;
    }
    return TFTP_ERR_NOT_FOUND;
}

static int drain_pipe(void* arg) {
    char buf[4096];
    int fd = (uintptr_t)arg;

    ssize_t sz;
    while ((sz = read(fd, buf, sizeof(buf) - 1)) > 0) {
        // ensure null termination
        buf[sz] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return sz;
}

static tftp_status file_open_write(const char* filename, size_t size,
                                   void* cookie) {
    file_info_t* file_info = cookie;
    file_info->is_write = true;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';

    const size_t image_prefix_len = strlen(NB_IMAGE_PREFIX);
    const size_t netboot_prefix_len = strlen(NB_FILENAME_PREFIX);
    if (netbootloader && !strncmp(filename, NB_FILENAME_PREFIX, netboot_prefix_len)) {
        // netboot
        file_info->type = netboot;
        file_info->netboot_file = netboot_get_buffer(filename, size);
        if (file_info->netboot_file != NULL) {
            return TFTP_NO_ERROR;
        }
    } else if (netbootloader & !strncmp(filename, NB_IMAGE_PREFIX, image_prefix_len)) {
        // paving an image to disk
        launchpad_t* lp;
        launchpad_create(0, "paver", &lp);
        const char* bin = "/boot/bin/install-disk-image";
        launchpad_load_from_file(lp, bin);
        if (!strcmp(filename + image_prefix_len, NB_FVM_HOST_FILENAME)) {
            printf("netsvc: Running FVM Paver\n");
            const char* args[] = {bin, "install-fvm"};
            launchpad_set_args(lp, 2, args);
        } else if (!strcmp(filename + image_prefix_len, NB_EFI_HOST_FILENAME)) {
            printf("netsvc: Running EFI Paver\n");
            const char* args[] = {bin, "install-efi"};
            launchpad_set_args(lp, 2, args);
        } else if (!strcmp(filename + image_prefix_len, NB_KERNC_HOST_FILENAME)) {
            printf("netsvc: Running KERN-C Paver\n");
            const char* args[] = {bin, "install-kernc"};
            launchpad_set_args(lp, 2, args);
        } else {
            fprintf(stderr, "netsvc: Unknown Paver\n");
            return TFTP_ERR_IO;
        }
        launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON);

        int fds[2];
        if (pipe(fds)) {
            return TFTP_ERR_IO;
        }
        launchpad_transfer_fd(lp, fds[0], STDIN_FILENO);

        int logfds[2];
        if (pipe(logfds)) {
            return TFTP_ERR_IO;
        }
        launchpad_transfer_fd(lp, logfds[1], STDERR_FILENO);

        if (launchpad_go(lp, &file_info->paver.process, NULL) != ZX_OK) {
            printf("netsvc: tftp couldn't launch paver\n");
            close(fds[1]);
            close(logfds[0]);
            return TFTP_ERR_IO;
        }

        thrd_t log_thrd;
        if ((thrd_create(&log_thrd, drain_pipe, (void*)(uintptr_t)logfds[0])) == thrd_success) {
            thrd_detach(log_thrd);
        } else {
            close(logfds[0]);
        }

        file_info->type = paver;
        file_info->paver.fd = fds[1];
        return TFTP_NO_ERROR;
    } else {
        // netcp
        if (netfile_open(filename, O_WRONLY, NULL) == 0) {
            return TFTP_NO_ERROR;
        }
    }
    return TFTP_ERR_INVALID_ARGS;
}

static tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
    if (length == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    int read_len = netfile_offset_read(data, offset, *length);
    if (read_len < 0) {
        return TFTP_ERR_IO;
    }
    *length = read_len;
    return TFTP_NO_ERROR;
}

static tftp_status file_write(const void* data, size_t* length, off_t offset, void* cookie) {
    if (length == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    file_info_t* file_info = cookie;
    if (file_info->type == netboot && file_info->netboot_file != NULL) {
        nbfile* nb_file = file_info->netboot_file;
        if (((size_t)offset > nb_file->size) || (offset + *length) > nb_file->size) {
            return TFTP_ERR_INVALID_ARGS;
        }
        memcpy(nb_file->data + offset, data, *length);
        nb_file->offset = offset + *length;
        return TFTP_NO_ERROR;
    } else if (file_info->type == paver) {
        size_t len = *length;
        while (len) {
            int r = write(file_info->paver.fd, data, len);
            if (r <= 0) {
                printf("netsvc: Couldn't write to paver fd: %d\n", r);
                return TFTP_ERR_IO;
            }
            len -= r;
            data += r;
        }
        return TFTP_NO_ERROR;
    } else {
        int write_result = netfile_offset_write(data, offset, *length);
        if ((size_t)write_result == *length) {
            return TFTP_NO_ERROR;
        }
        if (write_result == -EBADF) {
            return TFTP_ERR_BAD_STATE;
        }
        return TFTP_ERR_IO;
    }
}

static void file_close(void* cookie) {
    file_info_t* file_info = cookie;
    if (file_info->type == netboot && file_info->netboot_file == NULL) {
        netfile_close();
    } else if (file_info->type == paver && file_info->filename[0] != '\0') {
        zx_signals_t signals;
        close(file_info->paver.fd);
        zx_object_wait_one(file_info->paver.process, ZX_TASK_TERMINATED,
                           zx_deadline_after(ZX_SEC(10)), &signals);
        zx_handle_close(file_info->paver.process);
        // Extra protection against double-close.
        file_info->filename[0] = '\0';
    }
}

static tftp_status transport_send(void* data, size_t len, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    zx_status_t status = udp6_send(data, len, &transport_info->dest_addr,
                                   transport_info->dest_port, NB_TFTP_OUTGOING_PORT, true);
    if (status != ZX_OK) {
        return TFTP_ERR_IO;
    }

    // The timeout is relative to sending instead of receiving a packet, since there are some
    // received packets we want to ignore (duplicate ACKs).
    if (transport_info->timeout_ms != 0) {
        tftp_next_timeout = zx_deadline_after(ZX_MSEC(transport_info->timeout_ms));
        update_timeouts();
    }
    return TFTP_NO_ERROR;
}

static int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    transport_info->timeout_ms = timeout_ms;
    return 0;
}

static void initialize_connection(const ip6_addr_t* saddr, uint16_t sport) {
    int ret = tftp_init(&session, tftp_session_scratch,
                        sizeof(tftp_session_scratch));
    if (ret != TFTP_NO_ERROR) {
        printf("netsvc: failed to initiate tftp session\n");
        session = NULL;
        return;
    }

    // Initialize file interface
    file_init(&file_info);
    tftp_file_interface file_ifc = {file_open_read, file_open_write,
                                    file_read, file_write, file_close};
    tftp_session_set_file_interface(session, &file_ifc);

    // Initialize transport interface
    memcpy(&transport_info.dest_addr, saddr, sizeof(ip6_addr_t));
    transport_info.dest_port = sport;
    transport_info.timeout_ms = 1000; // Reasonable default for now
    tftp_transport_interface transport_ifc = {transport_send, NULL, transport_timeout_set};
    tftp_session_set_transport_interface(session, &transport_ifc);
}

static void end_connection(void* cookie) {
    file_close(cookie);
    session = NULL;
    tftp_next_timeout = ZX_TIME_INFINITE;
}

void tftp_timeout_expired(void) {
    tftp_status result = tftp_timeout(session, tftp_out_scratch, &last_msg_size,
                                      sizeof(tftp_out_scratch), &transport_info.timeout_ms,
                                      &file_info);
    if (result == TFTP_ERR_TIMED_OUT) {
        printf("netsvc: excessive timeouts, dropping tftp connection\n");
        end_connection(&file_info);
        netfile_abort_write();
    } else if (result < 0) {
        printf("netsvc: failed to generate timeout response, dropping tftp connection\n");
        end_connection(&file_info);
        netfile_abort_write();
    } else {
        if (last_msg_size > 0) {
            tftp_status send_result = transport_send(tftp_out_scratch, last_msg_size, &transport_info);
            if (send_result != TFTP_NO_ERROR) {
                printf("netsvc: failed to send tftp timeout response (err = %d)\n", send_result);
            }
        }
    }
}

void tftp_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
    if (dport == NB_TFTP_INCOMING_PORT) {
        if (session != NULL) {
            printf("netsvc: only one simultaneous tftp session allowed\n");
            // ignore attempts to connect when a session is in progress
            return;
        }
        initialize_connection(saddr, sport);
    } else if (!session) {
        // Ignore anything sent to the outgoing port unless we've already
        // established a connection.
        return;
    }

    last_msg_size = sizeof(tftp_out_scratch);

    char err_msg[128];
    tftp_handler_opts handler_opts = {.inbuf = data,
                                      .inbuf_sz = len,
                                      .outbuf = tftp_out_scratch,
                                      .outbuf_sz = &last_msg_size,
                                      .err_msg = err_msg,
                                      .err_msg_sz = sizeof(err_msg)};
    tftp_status status = tftp_handle_msg(session, &transport_info, &file_info,
                                         &handler_opts);
    if (status < 0) {
        printf("netsvc: tftp protocol error:%s\n", err_msg);
        end_connection(&file_info);
        netfile_abort_write();
    } else if (status == TFTP_TRANSFER_COMPLETED) {
        printf("netsvc: tftp %s of file %s completed\n",
               file_info.is_write ? "write" : "read",
               file_info.filename);
        end_connection(&file_info);
    }
}

bool tftp_has_pending(void) {
    return session && tftp_session_has_pending(session);
}

void tftp_send_next(void) {
    last_msg_size = sizeof(tftp_out_scratch);
    tftp_prepare_data(session, tftp_out_scratch, &last_msg_size, &transport_info.timeout_ms,
                      &file_info);
    if (last_msg_size) {
        transport_send(tftp_out_scratch, last_msg_size, &transport_info);
    }
}
