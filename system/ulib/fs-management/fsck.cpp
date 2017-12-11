// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <zircon/compiler.h>
#include <zircon/device/vfs.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <fdio/limits.h>
#include <fdio/util.h>
#include <fdio/vfs.h>

static zx_status_t fsck_mxfs(const char* devicepath, const fsck_options_t* options,
                              LaunchCallback cb, const char* cmdpath) {
    zx_handle_t hnd[FDIO_MAX_HANDLES * 2];
    uint32_t ids[FDIO_MAX_HANDLES * 2];
    size_t n = 0;
    int device_fd;
    if ((device_fd = open(devicepath, O_RDWR)) < 0) {
        fprintf(stderr, "Failed to open device\n");
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status;
    if ((status = fdio_transfer_fd(device_fd, FS_FD_BLOCKDEVICE, hnd + n, ids + n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        return status != 0 ? status : ZX_ERR_BAD_STATE;
    }
    n += status;

    const char** argv =
            reinterpret_cast<const char**>(calloc(sizeof(char*), (2 + NUM_FSCK_OPTIONS)));
    int argc = 0;
    argv[argc++] = cmdpath;
    if (options->verbose) {
        argv[argc++] = "-v";
    }
    // TODO(smklein): Add support for modify, force flags. Without them,
    // we have "always_modify=true" and "force=true" effectively on by default.
    argv[argc++] = "fsck";
    status = static_cast<zx_status_t>(cb(argc, argv, hnd, ids, n));
    free(argv);
    return status;
}

static zx_status_t fsck_fat(const char* devicepath, const fsck_options_t* options,
                            LaunchCallback cb) {
    const char** argv =
            reinterpret_cast<const char**>(calloc(sizeof(char*), (2 + NUM_FSCK_OPTIONS)));
    int argc = 0;
    argv[argc++] = "/boot/bin/fsck-msdosfs";
    if (options->never_modify) {
        argv[argc++] = "-n";
    } else if (options->always_modify) {
        argv[argc++] = "-y";
    }
    if (options->force) {
        argv[argc++] = "-f";
    }
    argv[argc++] = devicepath;
    zx_status_t status = static_cast<zx_status_t>(cb(argc, argv, NULL, NULL, 0));
    free(argv);
    return status;
}

zx_status_t fsck(const char* devicepath, disk_format_t df,
                 const fsck_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return fsck_mxfs(devicepath, options, cb, "/boot/bin/minfs");
    case DISK_FORMAT_FAT:
        return fsck_fat(devicepath, options, cb);
    case DISK_FORMAT_BLOBFS:
        return fsck_mxfs(devicepath, options, cb, "/boot/bin/blobstore");
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}
