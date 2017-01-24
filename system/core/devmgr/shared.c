// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpi.h"
#include "devhost.h"
#include "devmgr.h"

#include <stdio.h>

#include <launchpad/launchpad.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/log.h>
#include <mxio/remoteio.h>
#include <mxio/util.h>


#if DEVMGR
mx_handle_t vfs_create_global_root_handle(void);
#define LOG_FLAGS MX_LOG_FLAG_DEVMGR
#else
#define LOG_FLAGS MX_LOG_FLAG_DEVICE
#endif

void devmgr_io_init(void) {
    // setup stdout
    mx_handle_t h;
    if (mx_log_create(LOG_FLAGS, &h) < 0) {
        return;
    }
    mxio_t* logger;
    if ((logger = mxio_logger_create(h)) == NULL) {
        return;
    }
    close(1);
    mxio_bind_to_fd(logger, 1, 0);
}

extern mx_handle_t application_launcher;

void devmgr_launch_devhost(mx_handle_t job,
                           const char* name, int argc, char** argv,
                           mx_handle_t hdevice, mx_handle_t hrpc) {
    mx_handle_t hnd[8];
    uint32_t ids[8];
    ids[0] = MX_HND_INFO(MX_HND_TYPE_VDSO_VMO, 0);
    hnd[0] = launchpad_get_vdso_vmo();
    ids[1] = MX_HND_INFO(MX_HND_TYPE_USER0, ID_HDEVICE);
    hnd[1] = hdevice;
    ids[2] = MX_HND_INFO(MX_HND_TYPE_USER0, ID_HRPC);
    hnd[2] = hrpc;
    ids[3] = MX_HND_INFO(MX_HND_TYPE_RESOURCE, 0);
    hnd[3] = MX_HANDLE_INVALID;
    mx_handle_duplicate(get_root_resource(), MX_RIGHT_SAME_RIGHTS, &hnd[3]);
    ids[4] = MX_HND_TYPE_MXIO_ROOT;
#if DEVMGR
    hnd[4] = vfs_create_global_root_handle();
#else
    hnd[4] = 0;
    uint32_t type;
    mxio_clone_root(hnd + 4, &type);
#endif

    uint32_t hcount = 5;

#if !DEVMGR
    // pass acpi handle if available
    ids[hcount] = MX_HND_INFO(MX_HND_TYPE_USER0, ID_HACPI);
    hnd[hcount] = devmgr_acpi_clone();
    if (hnd[hcount] > 0) {
        hcount++;
    }
#else
    if (application_launcher > 0) {
        ids[hcount] = MX_HND_INFO(MX_HND_TYPE_USER0, ID_HLAUNCHER);
        hnd[hcount] = application_launcher;
        if (hnd[hcount] > 0) {
            hcount++;
        }
    }
#endif
    if ((hnd[hcount] = get_sysinfo_job_root()) != MX_HANDLE_INVALID) {
        ids[hcount] = MX_HND_INFO(MX_HND_TYPE_USER0, ID_HJOBROOT);
        hcount++;
    }
    mx_handle_t job_copy = MX_HANDLE_INVALID;
    mx_status_t status = mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_copy);
    if (status < 0) {
        printf("devmgr: no job, launch failed: %d\n", job_copy);
        return;
    }

    printf("devmgr: launch: %s %s %s\n", name, argv[0], argv[1]);
    mx_handle_t proc = launchpad_launch_with_job(job_copy, name, argc, (const char* const*)argv,
                                                 (const char* const*)environ, hcount, hnd, ids);
    if (proc < 0) {
        printf("devmgr: launch failed: %d\n", proc);
    } else {
        mx_handle_close(proc);
    }
}
