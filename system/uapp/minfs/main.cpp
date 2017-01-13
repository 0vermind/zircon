// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// for S_IF*
#define _XOPEN_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "minfs-private.h"
#ifndef __Fuchsia__
#include "host.h"
#endif

int do_minfs_check(Bcache* bc, int argc, char** argv) {
    return minfs_check(bc);
}

#ifdef __Fuchsia__
int do_minfs_mount(Bcache* bc, int argc, char** argv) {
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    vfs_rpc_server(vn);
    return 0;
}
#else
int run_fs_tests(int argc, char** argv);

static Bcache* the_block_cache;
void drop_cache(void) {
    the_block_cache->Invalidate();
}

extern vnode_t* fake_root;

int io_setup(Bcache* bc) {
    vnode_t* vn = 0;
    if (minfs_mount(&vn, bc) < 0) {
        return -1;
    }
    fake_root = vn;
    the_block_cache = bc;
    return 0;
}

int do_minfs_test(Bcache* bc, int argc, char** argv) {
    if (io_setup(bc)) {
        return -1;
    }
    return run_fs_tests(argc, argv);
}

int do_cp(Bcache* bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "cp requires two arguments\n");
        return -1;
    }

    if (io_setup(bc)) {
        return -1;
    }

    int fdi, fdo;
    if ((fdi = emu_open(argv[0], O_RDONLY, 0)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[0]);
        return -1;
    }
    if ((fdo = emu_open(argv[1], O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) {
        fprintf(stderr, "error: cannot open '%s'\n", argv[1]);
        return -1;
    }

    char buffer[256 * 1024];
    ssize_t r;
    for (;;) {
        if ((r = emu_read(fdi, buffer, sizeof(buffer))) < 0) {
            fprintf(stderr, "error: reading from '%s'\n", argv[0]);
            break;
        } else if (r == 0) {
            break;
        }
        void* ptr = buffer;
        ssize_t len = r;
        while (len > 0) {
            if ((r = emu_write(fdo, ptr, len)) < 0) {
                fprintf(stderr, "error: writing to '%s'\n", argv[1]);
                goto done;
            }
            ptr = (void*)((uintptr_t)ptr + r);
            len -= r;
        }
    }
done:
    emu_close(fdi);
    emu_close(fdo);
    return r;
}

int do_mkdir(Bcache* bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "mkdir requires one argument\n");
        return -1;
    }
    if (io_setup(bc)) {
        return -1;
    }
    // TODO(jpoichet) add support making parent directories when not present
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: mkdir can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_mkdir(path, 0);
}

int do_unlink(Bcache* bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "unlink requires one argument\n");
        return -1;
    }
    if (io_setup(bc)) {
        return -1;
    }
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: unlink can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_unlink(path);
}

int do_rename(Bcache* bc, int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "rename requires two arguments\n");
        return -1;
    }
    if (io_setup(bc)) {
        return -1;
    }
    const char* old_path = argv[0];
    const char* new_path = argv[1];
    if (strncmp(old_path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: rename can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    if (strncmp(new_path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: rename can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }
    return emu_rename(old_path, new_path);
}

static const char* modestr(uint32_t mode) {
    switch (mode & S_IFMT) {
    case S_IFREG:
        return "-";
    case S_IFCHR:
        return "c";
    case S_IFBLK:
        return "b";
    case S_IFDIR:
        return "d";
    default:
        return "?";
    }
}

int do_ls(Bcache* bc, int argc, char** argv) {
    if (argc != 1) {
        fprintf(stderr, "ls requires one argument\n");
        return -1;
    }
    if (io_setup(bc)) {
        return -1;
    }
    const char* path = argv[0];
    if (strncmp(path, PATH_PREFIX, PREFIX_SIZE)) {
        fprintf(stderr, "error: ls can only operate minfs paths (must start with %s)\n", PATH_PREFIX);
        return -1;
    }

    DIR* d = emu_opendir(path);
    if (!d) {
        return -1;
    }

    struct dirent* de;
    char tmp[2048];
    struct stat s;
    while ((de = emu_readdir(d)) != nullptr) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            memset(&s, 0, sizeof(struct stat));
            if ((strlen(de->d_name) + strlen(path) + 2) <= sizeof(tmp)) {
                snprintf(tmp, sizeof(tmp), "%s/%s", path, de->d_name);
                emu_stat(tmp, &s);
            }
            fprintf(stdout, "%s %8jd %s\n", modestr(s.st_mode), (intmax_t)s.st_size, de->d_name);
        }
    }
    emu_closedir(d);
    return 0;
}

#endif

int do_minfs_mkfs(Bcache* bc, int argc, char** argv) {
    return minfs_mkfs(bc);
}

struct {
    const char* name;
    int (*func)(Bcache* bc, int argc, char** argv);
    uint32_t flags;
    const char* help;
} CMDS[] = {
    {"create", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"mkfs", do_minfs_mkfs, O_RDWR | O_CREAT, "initialize filesystem"},
    {"check", do_minfs_check, O_RDONLY, "check filesystem integrity"},
    {"fsck", do_minfs_check, O_RDONLY, "check filesystem integrity"},
#ifdef __Fuchsia__
    {"mount", do_minfs_mount, O_RDWR, "mount filesystem"},
#else
    {"test", do_minfs_test, O_RDWR, "run tests against filesystem"},
    {"cp", do_cp, O_RDWR, "copy to/from fs"},
    {"mkdir", do_mkdir, O_RDWR, "create directory"},
    {"rm", do_unlink, O_RDWR, "delete file or directory"},
    {"unlink", do_unlink, O_RDWR, "delete file or directory"},
    {"mv", do_rename, O_RDWR, "rename file or directory"},
    {"rename", do_rename, O_RDWR, "rename file or directory"},
    {"ls", do_ls, O_RDWR, "list content of directory"},
#endif
};

int usage(void) {
    fprintf(stderr,
            "usage: minfs [ <option>* ] <file-or-device>[@<size>] <command> [ <arg>* ]\n"
            "\n"
            "options:  -v         some debug messages\n"
            "          -vv        all debug messages\n"
            "\n");
    for (unsigned n = 0; n < (sizeof(CMDS) / sizeof(CMDS[0])); n++) {
        fprintf(stderr, "%9s %-10s %s\n", n ? "" : "commands:",
                CMDS[n].name, CMDS[n].help);
    }
    fprintf(stderr, "\n");
    return -1;
}

off_t get_size(int fd) {
    struct stat s;
    if (fstat(fd, &s) < 0) {
        fprintf(stderr, "error: could not find end of file/device\n");
        return 0;
    }
    return s.st_size;
}

int do_bitmap_test(void);

int main(int argc, char** argv) {
    off_t size = 0;

    // handle options
    while (argc > 1) {
        if (!strcmp(argv[1], "-v")) {
            trace_on(TRACE_SOME);
        } else if (!strcmp(argv[1], "-vv")) {
            trace_on(TRACE_ALL);
        } else {
            break;
        }
        argc--;
        argv++;
    }

    if (argc < 3) {
        return usage();
    }

    char* fn = argv[1];
    char* cmd = argv[2];

    char* sizestr;
    if ((sizestr = strchr(fn, '@')) != nullptr) {
        *sizestr++ = 0;
        char* end;
        size = strtoull(sizestr, &end, 10);
        if (end == sizestr) {
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
            return usage();
        }
        switch (end[0]) {
        case 'M':
        case 'm':
            size *= (1024 * 1024);
            end++;
            break;
        case 'G':
        case 'g':
            size *= (1024 * 1024 * 1024);
            end++;
            break;
        }
        if (end[0]) {
            fprintf(stderr, "minfs: bad size: %s\n", sizestr);
            return usage();
        }
    }

    int fd;
    uint32_t flags = O_RDWR;
    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            flags = CMDS[i].flags;
            goto found;
        }
    }
    fprintf(stderr, "minfs: unknown command: %s\n", cmd);
    return usage();

found:
    if ((fd = open(fn, flags, 0644)) < 0) {
        if (flags & O_CREAT) {
            // temporary workaround for Magenta devfs issue
            flags &= (~O_CREAT);
            goto found;
        }
        fprintf(stderr, "error: cannot open '%s'\n", fn);
        return -1;
    }
    if (size == 0) {
        size = get_size(fd);
    }
    size /= kMinfsBlockSize;

    Bcache* bc;
    if (Bcache::Create(&bc, fd, (uint32_t) size, kMinfsBlockSize, 64) < 0) {
        fprintf(stderr, "error: cannot create block cache\n");
        return -1;
    }

    for (unsigned i = 0; i < sizeof(CMDS) / sizeof(CMDS[0]); i++) {
        if (!strcmp(cmd, CMDS[i].name)) {
            return CMDS[i].func(bc, argc - 3, argv + 3);
        }
    }
    return -1;
}
