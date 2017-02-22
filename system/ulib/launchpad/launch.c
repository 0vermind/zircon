// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launch.h"
#include <launchpad/vmo.h>

#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>

#include <mxio/util.h>
#include <threads.h>

mx_handle_t launchpad_launch_with_job(mx_handle_t job,
                                      const char* name,
                                      int argc, const char* const* argv,
                                      const char* const* envp,
                                      size_t hnds_count, mx_handle_t* handles,
                                      uint32_t* ids) {
    launchpad_t* lp;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    mx_status_t status = launchpad_create(job, name, &lp);
    if (status == NO_ERROR) {
        status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
        if (status == NO_ERROR)
            status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
        if (status == NO_ERROR)
            status = launchpad_set_args(lp, argc, argv);
        if (status == NO_ERROR)
            status = launchpad_set_environ(lp, envp);
        if (status == NO_ERROR)
            status = launchpad_add_handles(lp, hnds_count, handles, ids);
    }

    return finish_launch(lp, status, handles, hnds_count);
}

mx_handle_t launchpad_launch(const char* name,
                             int argc, const char* const* argv,
                             const char* const* envp,
                             size_t hnds_count, mx_handle_t* handles,
                             uint32_t* ids) {
    mx_handle_t job_to_child = MX_HANDLE_INVALID;
    mx_handle_t job = mx_job_default();
    if (job > 0)
        mx_handle_duplicate(job, MX_RIGHT_SAME_RIGHTS, &job_to_child);

    return launchpad_launch_with_job(job_to_child, name, argc, argv, envp, hnds_count, handles, ids);
}

mx_handle_t finish_launch(launchpad_t* lp, mx_status_t status,
                          mx_handle_t handles[], uint32_t handle_count) {
    mx_handle_t proc;
    if (status == NO_ERROR) {
        proc = launchpad_start(lp);
    } else {
        // Consume the handles on error.
        for (uint32_t i = 0; i < handle_count; ++i)
            mx_handle_close(handles[i]);
        proc = status;
    }
    if (lp != NULL)
        launchpad_destroy(lp);
    return proc;
}
