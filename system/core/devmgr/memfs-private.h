// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <fs/vfs.h>
#include <magenta/types.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>
#include <magenta/listnode.h>
#include <threads.h>

typedef struct dnode dnode_t;

#define MEMFS_TYPE_DATA 0
#define MEMFS_TYPE_DIR 1
#define MEMFS_TYPE_VMO 2
#define MEMFS_TYPE_DEVICE 3
#define MEMFS_TYPE_MASK 0x3
#define MEMFS_FLAG_VMO_REUSE 4

struct vnode {
    VNODE_BASE_FIELDS
    uint32_t seqcount;
    uint32_t memfs_flags; // type + flags

    dnode_t* dnode;      // list of my children

    // all dnodes that point at this vnode
    list_node_t dn_list;
    uint32_t dn_count;

    // all directory watchers
    list_node_t watch_list;

    mx_handle_t vmo;
    mx_off_t length; // TYPE_VMO: Size of data within vmo. TYPE_DATA: Size of vmo
    mx_off_t offset; // TYPE_VMO: Offset into vmo which contains data.

    uint64_t create_time;
    uint64_t modify_time;
};

typedef struct vnode_watcher {
    list_node_t node;
    mx_handle_t h;
} vnode_watcher_t;

mx_handle_t vfs_get_vmofile(vnode_t* vn, mx_off_t* off, mx_off_t* len);

void vfs_global_init(vnode_t* root);

// generate mxremoteio handles
mx_handle_t vfs_create_global_root_handle(void);
mx_handle_t vfs_create_root_handle(vnode_t* vn);

// vmo fs
ssize_t vmo_read(vnode_t* vn, void* data, size_t len, size_t off);
mx_status_t vmo_getattr(vnode_t* vn, vnattr_t* attr);

// device fs
vnode_t* devfs_get_root(void);
mx_status_t memfs_create_device_at(vnode_t* parent, vnode_t** out, const char* name, mx_handle_t hdevice);
mx_status_t devfs_remove(vnode_t* vn);

// boot fs
vnode_t* bootfs_get_root(void);
mx_status_t bootfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// system fs
vnode_t* systemfs_get_root(void);
mx_status_t systemfs_add_file(const char* path, mx_handle_t vmo, mx_off_t off, size_t len);

// memory fs
vnode_t* memfs_get_root(void);
mx_status_t memfs_add_link(vnode_t* parent, const char* name, vnode_t* target);
mx_status_t memfs_lookup_none(vnode_t* parent, vnode_t** out, const char* name, size_t len);
mx_status_t memfs_create_none(vnode_t* vndir, vnode_t** out, const char* name, size_t len, uint32_t mode);
ssize_t memfs_write_none(vnode_t* vn, const void* data, size_t len, size_t off);
ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off);
mx_status_t memfs_readdir_none(vnode_t* parent, void* cookie, void* data, size_t len);

// TODO(orr) normally static; temporary exposure, to be undone in subsequent patch
mx_status_t _memfs_create(vnode_t* parent, vnode_t** out,
                        const char* name, size_t namelen,
                        uint32_t type);

// Create the global root to memfs
vnode_t* vfs_create_global_root(void);

// Create a generic root to memfs
vnode_t* vfs_create_root(void);

void vfs_dump_handles(void);

// shared among all memory filesystems
mx_status_t memfs_open(vnode_t** _vn, uint32_t flags);
mx_status_t memfs_close(vnode_t* vn);
mx_status_t memfs_lookup(vnode_t* parent, vnode_t** out, const char* name, size_t len);
mx_status_t memfs_readdir(vnode_t* parent, void* cookie, void* data, size_t len);
mx_status_t memfs_truncate_none(vnode_t* vn, size_t len);
mx_status_t memfs_rename_none(vnode_t* olddir, vnode_t* newdir, const char* oldname, size_t oldlen,
                              const char* newname, size_t newlen, bool src_must_be_dir,
                              bool dst_must_be_dir);
ssize_t memfs_read_none(vnode_t* vn, void* data, size_t len, size_t off);
ssize_t memfs_write_none(vnode_t* vn, const void* data, size_t len, size_t off);
mx_status_t memfs_unlink(vnode_t* vn, const char* name, size_t len, bool must_be_dir);
ssize_t memfs_ioctl(vnode_t* vn, uint32_t op, const void* in_data,
                    size_t in_len, void* out_data, size_t out_len);
mx_status_t memfs_lookup_name(const vnode_t* vn, char* outname, size_t out_len);

mx_status_t memfs_create_from_buffer(const char* path, uint32_t flags,
                                     const char* ptr, mx_off_t len);
mx_status_t memfs_create_directory(const char* path, uint32_t flags);
mx_status_t memfs_create_from_vmo(const char* path, uint32_t flags,
                                  mx_handle_t vmo, mx_off_t off, mx_off_t len);

// big vfs lock protects lookup and walk operations
//TODO: finer grained locking
extern mtx_t vfs_lock;

vfs_iostate_t* create_vfs_iostate(mx_device_t* dev);
void memfs_mount(vnode_t* parent, vnode_t* subtree);
