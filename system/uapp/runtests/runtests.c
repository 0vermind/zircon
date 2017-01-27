// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <limits.h>
#include <magenta/listnode.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct failure {
    list_node_t node;
    int cause;
    int rc;
    char name[0];
} failure_t;

static void fail_test(list_node_t* failures, const char* name, int cause, int rc) {
    size_t name_len = strlen(name) + 1;
    failure_t* failure = malloc(sizeof(failure_t) + name_len);
    failure->cause = cause;
    failure->rc = rc;
    memcpy(failure->name, name, name_len);
    list_add_tail(failures, &failure->node);
}

enum {
    FAILED_TO_LAUNCH,
    FAILED_TO_WAIT,
    FAILED_TO_RETURN_CODE,
    FAILED_NONZERO_RETURN_CODE,
};

static list_node_t failures = LIST_INITIAL_VALUE(failures);

static int total_count = 0;
static int failed_count = 0;

// We want the default to be the same, whether the test is run by us
// or run standalone. Do this by leaving the verbosity unspecified unless
// provided by the user.
static int verbosity = -1;

static void run_tests(const char* dirn) {
    DIR* dir = opendir(dirn);
    if (dir == NULL) {
        return;
    }

    struct dirent* de;
    struct stat stat_buf;
    while ((de = readdir(dir)) != NULL) {
        char name[64 + NAME_MAX];
        snprintf(name, sizeof(name), "%s/%s", dirn, de->d_name);
        if (stat(name, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
            continue;
        }

        total_count++;
        if (verbosity) {
            printf(
                "\n------------------------------------------------\n"
                "RUNNING TEST: %s\n\n",
                de->d_name);
        }

        char verbose_opt[] = {'v','=', verbosity + '0', 0};
        const char* argv[] = {name, verbose_opt};
        int argc = verbosity >= 0 ? 2 : 1;

        launchpad_t* lp;
        launchpad_create(0, name, &lp);
        launchpad_load_from_file(lp, argv[0]);
        launchpad_clone(lp, LP_CLONE_ALL);
        launchpad_set_args(lp, argc, argv);
        const char* errmsg;
        mx_handle_t handle;
        mx_status_t status = launchpad_go(lp, &handle, &errmsg);
        if (status < 0) {
            printf("FAILURE: Failed to launch %s: %d: %s\n", de->d_name, status, errmsg);
            fail_test(&failures, de->d_name, FAILED_TO_LAUNCH, 0);
            failed_count++;
            continue;
        }

        status = mx_handle_wait_one(handle, MX_PROCESS_SIGNALED,
                                    MX_TIME_INFINITE, NULL);
        if (status != NO_ERROR) {
            printf("FAILURE: Failed to wait for process exiting %s: %d\n", de->d_name, status);
            fail_test(&failures, de->d_name, FAILED_TO_WAIT, 0);
            failed_count++;
            continue;
        }

        // read the return code
        mx_info_process_t proc_info;
        status = mx_object_get_info(handle, MX_INFO_PROCESS, &proc_info, sizeof(proc_info), NULL, NULL);
        mx_handle_close(handle);

        if (status < 0) {
            printf("FAILURE: Failed to get process return code %s: %d\n", de->d_name, status);
            fail_test(&failures, de->d_name, FAILED_TO_RETURN_CODE, 0);
            failed_count++;
            continue;
        }

        if (proc_info.return_code == 0) {
            printf("PASSED: %s passed\n", de->d_name);
        } else {
            printf("FAILED: %s exited with nonzero status: %d\n", de->d_name, proc_info.return_code);
            fail_test(&failures, de->d_name, FAILED_NONZERO_RETURN_CODE, proc_info.return_code);
            failed_count++;
        }
    }

    closedir(dir);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "-q") == 0) {
            verbosity = 0;
        } else if (strcmp(argv[1], "-v") == 0) {
            printf("verbose output. enjoy.\n");
            verbosity = 1;
        } else {
            printf("unknown option. usage: %s [-q|-v]\n", argv[0]);
            return -1;
        }
    }

    run_tests("/boot/test");
    run_tests("/system/test");

    printf("\nSUMMARY: Ran %d tests: %d failed\n", total_count, failed_count);

    if (failed_count) {
        printf("\nThe following tests failed:\n");
        failure_t* failure = NULL;
        failure_t* temp = NULL;
        list_for_every_entry_safe (&failures, failure, temp, failure_t, node) {
            switch (failure->cause) {
            case FAILED_TO_LAUNCH:
                printf("%s: failed to launch\n", failure->name);
                break;
            case FAILED_TO_WAIT:
                printf("%s: failed to wait\n", failure->name);
                break;
            case FAILED_TO_RETURN_CODE:
                printf("%s: failed to return exit code\n", failure->name);
                break;
            case FAILED_NONZERO_RETURN_CODE:
                printf("%s: returned nonzero: %d\n", failure->name, failure->rc);
                break;
            }
            free(failure);
        }
    }

    return 0;
}
