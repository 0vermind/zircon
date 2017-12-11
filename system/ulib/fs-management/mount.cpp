// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs-management/mount.h>
#include <fs/client.h>

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

disk_format_t detect_disk_format(int fd) {
    uint8_t data[HEADER_SIZE];
    if (read(fd, data, sizeof(data)) != sizeof(data)) {
        fprintf(stderr, "Error reading block device\n");
        return DISK_FORMAT_UNKNOWN;
    }

    if (!memcmp(data, fvm_magic, sizeof(fvm_magic))) {
        return DISK_FORMAT_FVM;
    } else if (!memcmp(data + 0x200, gpt_magic, sizeof(gpt_magic))) {
        return DISK_FORMAT_GPT;
    } else if (!memcmp(data, minfs_magic, sizeof(minfs_magic))) {
        return DISK_FORMAT_MINFS;
    } else if (!memcmp(data, blobstore_magic, sizeof(blobstore_magic))) {
        return DISK_FORMAT_BLOBFS;
    } else if ((data[510] == 0x55 && data[511] == 0xAA)) {
        if ((data[38] == 0x29 || data[66] == 0x29)) {
            // 0x55AA are always placed at offset 510 and 511 for FAT filesystems.
            // 0x29 is the Boot Signature, but it is placed at either offset 38 or
            // 66 (depending on FAT type).
            return DISK_FORMAT_FAT;
        } else {
            return DISK_FORMAT_MBR;
        }
    }
    return DISK_FORMAT_UNKNOWN;
}

// Initializes 'hnd' and 'ids' with the root handle and block device handle.
// Consumes devicefd.
static zx_status_t mount_prepare_handles(int devicefd, zx_handle_t* mount_handle_out,
                                         zx_handle_t* hnd, uint32_t* ids, size_t* n) {
    zx_status_t status;
    zx_handle_t mountee_handle;
    if ((status = zx_channel_create(0, &mountee_handle, mount_handle_out)) != ZX_OK) {
        close(devicefd);
        return status;
    }
    hnd[*n] = mountee_handle;
    ids[*n] = PA_USER0;
    *n = *n + 1;

    if ((status = fdio_transfer_fd(devicefd, FS_FD_BLOCKDEVICE, hnd + *n, ids + *n)) <= 0) {
        fprintf(stderr, "Failed to access device handle\n");
        zx_handle_close(mountee_handle);
        zx_handle_close(*mount_handle_out);
        close(devicefd);
        return status != 0 ? status : ZX_ERR_BAD_STATE;
    }
    *n = *n + status;
    return ZX_OK;
}

// Describes the mountpoint of the to-be-mounted root,
// either by fd or by path (but never both).
typedef struct mountpoint {
    union {
        const char* path;
        int fd;
    };
    uint32_t flags;
} mountpoint_t;

// Calls the 'launch callback' and mounts the remote handle to the target vnode, if successful.
static zx_status_t launch_and_mount(LaunchCallback cb, const mount_options_t* options,
                                    const char** argv, int argc, zx_handle_t* hnd,
                                    uint32_t* ids, size_t n, mountpoint_t* mp, zx_handle_t root) {
    zx_status_t status;
    if ((status = cb(argc, argv, hnd, ids, n)) != ZX_OK) {
        goto fail;
    }

    if (options->wait_until_ready) {
        // Wait until the filesystem is ready to take incoming requests
        zx_signals_t observed;
        status = zx_object_wait_one(root, ZX_USER_SIGNAL_0 | ZX_CHANNEL_PEER_CLOSED,
                                    ZX_TIME_INFINITE, &observed);
        if ((status != ZX_OK) || (observed & ZX_CHANNEL_PEER_CLOSED)) {
            status = (status != ZX_OK) ? status : ZX_ERR_BAD_STATE;
            goto fail;
        }
    }

    // Install remote handle.
    if (options->create_mountpoint) {
        int fd = open("/", O_RDONLY | O_DIRECTORY | O_ADMIN);
        if (fd < 0) {
            goto fail;
        }

        size_t config_size = sizeof(mount_mkdir_config_t) + strlen(mp->path) + 1;
        mount_mkdir_config_t* config = reinterpret_cast<mount_mkdir_config_t*>(malloc(config_size));
        if (config == NULL) {
            close(fd);
            goto fail;
        }
        config->fs_root = root;
        config->flags = mp->flags;
        strcpy(config->name, mp->path);
        // Ioctl will close root for us if an error occurs
        status = static_cast<zx_status_t>(ioctl_vfs_mount_mkdir_fs(fd, config, config_size));
        close(fd);
        free(config);
        return status;
    }
    // Ioctl will close root for us if an error occurs
    return static_cast<zx_status_t>(ioctl_vfs_mount_fs(mp->fd, &root));
fail:
    // We've entered a failure case where the filesystem process (which may or may not be alive)
    // had a *chance* to be spawned, but cannot be attached to a vnode (for whatever reason).
    // Rather than abandoning the filesystem process (maybe causing dirty bits to be set), give it a
    // chance to shutdown properly.
    //
    // The unmount process is a little atypical, since we're just sending a signal over a handle,
    // rather than detaching the mounted filesytem from the "parent" filesystem.
    vfs_unmount_handle(root, options->wait_until_ready ? ZX_TIME_INFINITE : 0);
    return status;
}

static zx_status_t mount_mxfs(const char* binary, int devicefd, mountpoint_t* mp,
                              const mount_options_t* options, LaunchCallback cb) {
    zx_handle_t hnd[FDIO_MAX_HANDLES * 2];
    uint32_t ids[FDIO_MAX_HANDLES * 2];
    size_t n = 0;
    zx_handle_t root;
    zx_status_t status;
    if ((status = mount_prepare_handles(devicefd, &root, hnd, ids, &n)) != ZX_OK) {
        return status;
    }

    if (options->verbose_mount) {
        printf("fs_mount: Launching %s\n", binary);
    }
    const char* argv[3] = {binary};
    int argc = 1;
    if (options->readonly) {
        argv[argc++] = "--readonly";
    }
    argv[argc++] = "mount";
    return launch_and_mount(cb, options, argv, argc, hnd, ids, n, mp, root);
}

static zx_status_t mount_fat(int devicefd, mountpoint_t* mp, const mount_options_t* options,
                             LaunchCallback cb) {
    zx_handle_t hnd[FDIO_MAX_HANDLES * 2];
    uint32_t ids[FDIO_MAX_HANDLES * 2];
    size_t n = 0;
    zx_handle_t root;
    zx_status_t status;
    if ((status = mount_prepare_handles(devicefd, &root, hnd, ids, &n)) != ZX_OK) {
        return status;
    }

    char readonly_arg[64];
    snprintf(readonly_arg, sizeof(readonly_arg), "-readonly=%s",
             options->readonly ? "true" : "false");
    char blockfd_arg[64];
    snprintf(blockfd_arg, sizeof(blockfd_arg), "-blockFD=%d", FS_FD_BLOCKDEVICE);

    if (options->verbose_mount) {
        printf("fs_mount: Launching ThinFS\n");
    }
    const char* argv[] = {
        "/system/bin/thinfs",
        readonly_arg,
        blockfd_arg,
        "mount",
    };
    return launch_and_mount(cb, options, argv, countof(argv), hnd, ids, n, mp, root);
}

zx_status_t fmount_common(int devicefd, mountpoint_t* mp, disk_format_t df,
                          const mount_options_t* options, LaunchCallback cb) {
    switch (df) {
    case DISK_FORMAT_MINFS:
        return mount_mxfs("/boot/bin/minfs", devicefd, mp, options, cb);
    case DISK_FORMAT_BLOBFS:
        return mount_mxfs("/boot/bin/blobstore", devicefd, mp, options, cb);
    case DISK_FORMAT_FAT:
        return mount_fat(devicefd, mp, options, cb);
    default:
        close(devicefd);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t fmount(int devicefd, int mountfd, disk_format_t df,
                   const mount_options_t* options, LaunchCallback cb) {
    mountpoint_t mp;
    mp.fd = mountfd;
    mp.flags = 0;

    return fmount_common(devicefd, &mp, df, options, cb);
}

zx_status_t mount(int devicefd, const char* mountpath, disk_format_t df,
                  const mount_options_t* options, LaunchCallback cb) {
    mountpoint_t mp;
    mp.flags = 0;

    if (options->create_mountpoint) {
        // Using 'path' for mountpoint
        mp.path = mountpath;
    } else {
        // Open mountpoint; use it directly
        if ((mp.fd = open(mountpath, O_RDONLY | O_DIRECTORY | O_ADMIN)) < 0) {
            return ZX_ERR_BAD_STATE;
        }
    }

    zx_status_t status = fmount_common(devicefd, &mp, df, options, cb);
    if (!options->create_mountpoint) {
        close(mp.fd);
    }
    return status;
}

zx_status_t fumount(int mountfd) {
    zx_handle_t h;
    zx_status_t status = static_cast<zx_status_t>(ioctl_vfs_unmount_node(mountfd, &h));
    if (status < 0) {
        fprintf(stderr, "Could not unmount filesystem: %d\n", status);
    } else {
        status = vfs_unmount_handle(h, ZX_TIME_INFINITE);
    }
    return status;
}

zx_status_t umount(const char* mountpath) {
    int fd = open(mountpath, O_DIRECTORY | O_NOREMOTE | O_ADMIN);
    if (fd < 0) {
        fprintf(stderr, "Could not open directory: %s\n", strerror(errno));
        return ZX_ERR_BAD_STATE;
    }
    zx_status_t status = fumount(fd);
    close(fd);
    return status;
}
