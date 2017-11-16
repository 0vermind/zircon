// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <launchpad/launchpad.h>
#include <fdio/namespace.h>
#include <zircon/syscalls.h>

void print_namespace(fdio_flat_namespace_t* flat) {
    for (size_t n = 0; n < flat->count; n++) {
        fprintf(stderr, "{ .handle = 0x%08x, type = 0x%08x, .path = '%s' },\n",
                flat->handle[n], flat->type[n], flat->path[n]);
    }
}

int run_in_namespace(const char* bin, size_t count, char** mapping) {
    fdio_ns_t* ns;
    zx_status_t r;
    if ((r = fdio_ns_create(&ns)) < 0) {
        fprintf(stderr, "failed to create namespace: %d\n", r);
        return -1;
    }
    for (size_t n = 0; n < count; n++) {
        char* dst = *mapping++;
        char* src = strchr(dst, '=');
        if (src == NULL) {
            fprintf(stderr, "error: mapping '%s' not in form of '<dst>=<src>'\n", dst);
            return -1;
        }
        *src++ = 0;
        int fd = open(src, O_RDONLY | O_DIRECTORY);
        if (fd < 0) {
            fprintf(stderr, "error: cannot open '%s'\n", src);
            return -1;
        }
        if ((r = fdio_ns_bind_fd(ns, dst, fd)) < 0) {
            fprintf(stderr, "error: binding fd %d to '%s' failed: %d\n", fd, dst, r);
            close(fd);
            return -1;
        }
        close(fd);
    }
    fdio_flat_namespace_t* flat;
    fdio_ns_opendir(ns);
    r = fdio_ns_export(ns, &flat);
    fdio_ns_destroy(ns);
    if (r < 0) {
        fprintf(stderr, "error: cannot flatten namespace: %d\n", r);
        return -1;
    }

    print_namespace(flat);

    launchpad_t* lp;
    launchpad_create(0, bin, &lp);
    launchpad_clone(lp, LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON | LP_CLONE_DEFAULT_JOB);
    launchpad_set_args(lp, 1, &bin);
    launchpad_set_nametable(lp, flat->count, flat->path);
    launchpad_add_handles(lp, flat->count, flat->handle, flat->type);
    launchpad_load_from_file(lp, bin);
    free(flat);
    const char* errmsg;
    zx_handle_t proc;
    if ((r = launchpad_go(lp, &proc, &errmsg)) < 0) {
        fprintf(stderr, "error: failed to launch shell: %s\n", errmsg);
        return -1;
    }
    zx_object_wait_one(proc, ZX_PROCESS_TERMINATED, ZX_TIME_INFINITE, NULL);
    fprintf(stderr, "[done]\n");
    return 0;
}

int dump_current_namespace(void) {
    fdio_flat_namespace_t* flat;
    zx_status_t r = fdio_ns_export_root(&flat);

    if (r < 0) {
        fprintf(stderr, "error: cannot export namespace: %d\n", r);
        return -1;
    }

    print_namespace(flat);
    return 0;
}

int main(int argc, char** argv) {
    if (argc == 2 && strcmp(argv[1], "--dump") == 0) {
        return dump_current_namespace();
    }

    if (argc > 1) {
        return run_in_namespace("/boot/bin/sh", argc - 1, argv + 1);
    }

    printf("Usage: %s [ --dump | [dst=src]+ ]\n"
           "Dumps the current namespace or runs a shell with src mapped to dst\n",
           argv[0]);
    return -1;
}
