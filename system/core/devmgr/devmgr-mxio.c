// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devmgr.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <launchpad/launchpad.h>

#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/io.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct bootfile bootfile_t;
struct bootfile {
    bootfile_t* next;
    const char* name;
    void* data;
    size_t len;
};

struct callback_data {
    mx_handle_t vmo;
    unsigned int file_count;
    mx_status_t (*add_file)(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);
};

static void callback(void* arg, const char* path, size_t off, size_t len) {
    struct callback_data* cd = arg;
    //printf("bootfs: %s @%zd (%zd bytes)\n", path, off, len);
    cd->add_file(path, cd->vmo, off, len);
    ++cd->file_count;
}

static const char* env[] = {
#if !(defined(__x86_64__) || defined(__aarch64__))
    // make debugging less painful
    "LD_DEBUG=1",
#endif
    NULL,
};

#define USER_MAX_HANDLES 4

void devmgr_launch(mx_handle_t job,
                   const char* name, int argc, const char** argv, int stdiofd,
                   mx_handle_t* handles, uint32_t* types, size_t len) {
    mx_handle_t hnd[2 * VFS_MAX_HANDLES + USER_MAX_HANDLES];
    uint32_t ids[2 * VFS_MAX_HANDLES + USER_MAX_HANDLES];
    unsigned n = 1;
    mx_status_t r;

    ids[0] = MX_HND_TYPE_MXIO_ROOT;
    hnd[0] = vfs_create_global_root_handle();

    hnd[n] = launchpad_get_vdso_vmo();
    if (hnd[n] > 0) {
        ids[n++] = MX_HND_INFO(MX_HND_TYPE_VDSO_VMO, 0);
    } else {
        printf("devmgr: launchpad_get_vdso_vmo failed (%d)\n", hnd[n]);
    }

    if (stdiofd < 0) {
        // use system log for stdio
        ids[n] = MX_HND_INFO(MX_HND_TYPE_MXIO_LOGGER, MXIO_FLAG_USE_FOR_STDIO | 1);
        if ((hnd[n] = mx_log_create(0)) < 0) {
            goto fail;
        }
        n++;
    } else {
        // use provided fd for stdio
        r = mxio_clone_fd(stdiofd, MXIO_FLAG_USE_FOR_STDIO | 0, hnd + n, ids + n);
        close(stdiofd);
        if (r < 0) {
            goto fail;
        }
        n += r;
    }
    if (len > USER_MAX_HANDLES) {
        goto fail;
    }
    for (size_t i = 0; i < len; i++) {
        hnd[n] = handles[i];
        ids[n] = types[i];
        n++;
    }
    printf("devmgr: launch %s (%s)\n", argv[0], name);

    mx_handle_t job_copy;
    r = mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy);
    if (r < 0)
        goto fail;

    mx_handle_t proc = launchpad_launch_with_job(job_copy, name, argc, argv, env, n, hnd, ids);
    if (proc < 0) {
        printf("devmgr: launchpad_launch failed: %d\n", proc);
    } else {
        mx_handle_close(proc);
    }
    return;
fail:
    while (n > 0) {
        n--;
        mx_handle_close(hnd[n]);
    }
}

static unsigned int setup_bootfs_vmo(unsigned int n, mx_handle_t vmo) {
    uint64_t size;
    mx_status_t status = mx_vmo_get_size(vmo, &size);
    if (status != NO_ERROR) {
        printf("devmgr: failed to get bootfs #%u size (%d)\n", n, status);
        return 0;
    }
    if (size == 0)
        return 0;
    struct callback_data cd = {
        .vmo = vmo,
        .add_file = (n > 0) ? systemfs_add_file : bootfs_add_file,
    };
    bootfs_parse(vmo, size, &callback, &cd);
    return cd.file_count;
}

static void setup_bootfs(void) {
    mx_handle_t vmo;
    for (unsigned int n = 0;
         (vmo = mxio_get_startup_handle(
             MX_HND_INFO(MX_HND_TYPE_BOOTFS_VMO, n))) != MX_HANDLE_INVALID;
        ++n) {
        unsigned int count = setup_bootfs_vmo(n, vmo);
        if (count > 0)
            printf("devmgr: bootfs #%u contains %u file%s\n",
                   n, count, (count == 1) ? "" : "s");
    }
}

void devmgr_vfs_init(void) {
    printf("devmgr: vfs init\n");

    setup_bootfs();

    vfs_global_init(vfs_create_global_root());

    // give our own process access to files in the vfs
    mx_handle_t h = vfs_create_global_root_handle();
    if (h > 0) {
        mxio_install_root(mxio_remote_create(h, 0));
    }
}

void devmgr_vfs_exit(void) {
    vfs_uninstall_all();
}
