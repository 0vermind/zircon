// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dnode.h"
#include "memfs-private.h"

#include <fs/vfs.h>

#include <magenta/device/devmgr.h>
#include <magenta/listnode.h>

#include <ddk/device.h>

#include <mxio/debug.h>
#include <mxio/remoteio.h>
#include <mxio/vfs.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Non-intrusive node in linked list of vnodes acting as mount points
typedef struct mount_node {
    list_node_t node;
    vnode_t* vn;
} mount_node_t;

static list_node_t remote_list = LIST_INITIAL_VALUE(remote_list);

// Installs a remote filesystem on vn and adds it to the remote_list.
mx_status_t vfs_install_remote(vnode_t* vn, mx_handle_t h) {
    if (vn == NULL) {
        return ERR_ACCESS_DENIED;
    }

    mtx_lock(&vfs_lock);
    // We cannot mount if anything else is already installed remotely
    if (vn->remote > 0) {
        mtx_unlock(&vfs_lock);
        return ERR_ALREADY_BOUND;
    }
    // Allocate a node to track the remote handle
    mount_node_t* mount_point;
    if ((mount_point = calloc(1, sizeof(mount_node_t))) == NULL) {
        mtx_unlock(&vfs_lock);
        return ERR_NO_MEMORY;
    }
    // Save this node in the list of mounted vnodes
    mount_point->vn = vn;
    list_add_tail(&remote_list, &mount_point->node);
    vn->remote = h;
    mtx_unlock(&vfs_lock);

    return NO_ERROR;
}

// Sends an 'unmount' signal on the srv handle, and waits until it is closed.
static mx_status_t txn_unmount(mx_handle_t srv) {
    mx_status_t r;
    mx_handle_t rchannel0, rchannel1;
    if ((r = mx_channel_create(MX_FLAG_REPLY_CHANNEL, &rchannel0, &rchannel1)) < 0) {
        return r;
    }
    mxrio_msg_t msg;
    memset(&msg, 0, MXRIO_HDR_SZ);
    msg.op = MXRIO_IOCTL;
    msg.arg2.op = IOCTL_DEVMGR_UNMOUNT_FS;
    if ((r = mxrio_txn_handoff(srv, rchannel1, &msg)) < 0) {
        mx_handle_close(rchannel0);
        mx_handle_close(rchannel1);
        return r;
    }
    r = mx_handle_wait_one(rchannel0, MX_CHANNEL_PEER_CLOSED | MX_CHANNEL_READABLE,
                           MX_TIME_INFINITE, NULL);
    // At the moment, we don't actually care what the response is from the
    // filesystem server (or even if it supports the unmount operation). As soon
    // as ANY response comes back, either in the form of a closed reply handle
    // or a visible response, shut down.
    mx_handle_close(rchannel0);
    return r;
}

static mx_status_t do_unmount(mount_node_t* mount_point) {
    mx_status_t status = NO_ERROR;
    if ((status = txn_unmount(mount_point->vn->remote)) < 0) {
        printf("Unexpected error unmounting filesystem: %d\n", status);
    }
    mx_handle_close(mount_point->vn->remote);
    mount_point->vn->remote = 0;
    free(mount_point);
    return status;
}

// Uninstall the remote filesystem mounted on vn. Removes vn from the
// remote_list, and sends its corresponding filesystem an 'unmount' signal.
mx_status_t vfs_uninstall_remote(vnode_t* vn) {
    mount_node_t* mount_point;
    mount_node_t* tmp;
    mx_status_t status = NO_ERROR;
    mtx_lock(&vfs_lock);
    list_for_every_entry_safe (&remote_list, mount_point, tmp, mount_node_t, node) {
        if (mount_point->vn == vn) {
            list_delete(&mount_point->node);
            goto done;
        }
    }
    status = ERR_NOT_FOUND;
done:
    mtx_unlock(&vfs_lock);
    if (status != NO_ERROR) {
        return status;
    }
    return do_unmount(mount_point);
}

// Uninstall all remote filesystems. Acts like 'vfs_uninstall_remote' for all
// known remotes.
mx_status_t vfs_uninstall_all() {
    mount_node_t* mount_point;
    for (;;) {
        mtx_lock(&vfs_lock);
        mount_point = list_remove_head_type(&remote_list, mount_node_t, node);
        mtx_unlock(&vfs_lock);
        if (mount_point) {
            do_unmount(mount_point);
        } else {
            return NO_ERROR;
        }
    }
}
