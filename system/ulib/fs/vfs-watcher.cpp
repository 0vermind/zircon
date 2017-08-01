// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __Fuchsia__
#include <magenta/device/vfs.h>
#include <magenta/syscalls.h>
#include <mxtl/auto_lock.h>
#endif

#include <fs/vfs.h>
#include <mx/channel.h>
#include <mxalloc/new.h>

namespace fs {

VnodeWatcher::VnodeWatcher(mx::channel h, uint32_t mask) : h(mxtl::move(h)),
    mask(mask & ~(VFS_WATCH_MASK_EXISTING | VFS_WATCH_MASK_IDLE)) {}

VnodeWatcher::~VnodeWatcher() {}

mx_status_t WatchBuffer::AddMsg(const mx::channel& c, unsigned event, const char* name) {
    size_t slen = strlen(name);
    size_t mlen = sizeof(vfs_watch_msg_t) + slen;
    if (mlen + watch_buf_size_ > sizeof(watch_buf_)) {
        // This message won't fit in the watch_buf; transmit first.
        mx_status_t status = Send(c);
        if (status != MX_OK) {
            return status;
        }
    }
    vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>((uintptr_t)watch_buf_ + watch_buf_size_);
    vmsg->event = static_cast<uint8_t>(event);
    vmsg->len = static_cast<uint8_t>(slen);
    memcpy(vmsg->name,name, slen);
    watch_buf_size_ += mlen;
    return MX_OK;
}

mx_status_t WatchBuffer::Send(const mx::channel& c) {
    if (watch_buf_size_ > 0) {
        // Only write if we have something to write
        mx_status_t status = c.write(0, watch_buf_, static_cast<uint32_t>(watch_buf_size_), nullptr, 0);
        watch_buf_size_ = 0;
        if (status != MX_OK) {
            return status;
        }
    }
    return MX_OK;
}

mx_status_t WatcherContainer::WatchDir(mx::channel* out) {
    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(mx::channel(),
                                                                  VFS_WATCH_MASK_ADDED));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }
    mx::channel out_channel;
    if (mx::channel::create(0, &out_channel, &watcher->h) != MX_OK) {
        return MX_ERR_NO_RESOURCES;
    }
    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    *out = mxtl::move(out_channel);
    return MX_OK;
}

mx_status_t WatcherContainer::WatchDirV2(Vfs* vfs, Vnode* vn, const vfs_watch_dir_t* cmd) {
    mx::channel c = mx::channel(cmd->channel);
    if ((cmd->mask & VFS_WATCH_MASK_ALL) == 0) {
        // No events to watch
        return MX_ERR_INVALID_ARGS;
    }

    AllocChecker ac;
    mxtl::unique_ptr<VnodeWatcher> watcher(new (&ac) VnodeWatcher(mxtl::move(c), cmd->mask));
    if (!ac.check()) {
        return MX_ERR_NO_MEMORY;
    }

    if (cmd->mask & VFS_WATCH_MASK_EXISTING) {
        vdircookie_t dircookie;
        memset(&dircookie, 0, sizeof(dircookie));
        char readdir_buf[MXIO_CHUNK_SIZE];
        WatchBuffer wb;
        {
            // Send "VFS_WATCH_EVT_EXISTING" for all entries in readdir
            mxtl::AutoLock lock(&vfs->vfs_lock_);
            while (true) {
                mx_status_t status = vn->Readdir(&dircookie, &readdir_buf, sizeof(readdir_buf));
                if (status <= 0) {
                    break;
                }
                void* ptr = readdir_buf;
                while (status > 0) {
                    auto dirent = reinterpret_cast<vdirent_t*>(ptr);
                    if (dirent->name[0]) {
                        wb.AddMsg(watcher->h, VFS_WATCH_EVT_EXISTING, dirent->name);
                    }
                    status -= dirent->size;
                    ptr = reinterpret_cast<void*>(
                            static_cast<uintptr_t>(dirent->size) +
                            reinterpret_cast<uintptr_t>(ptr));
                }
            }
        }

        // Send VFS_WATCH_EVT_IDLE to signify that readdir has completed
        if (cmd->mask & VFS_WATCH_MASK_IDLE) {
            wb.AddMsg(watcher->h, VFS_WATCH_EVT_IDLE, "");
        }

        wb.Send(watcher->h);
    }

    mxtl::AutoLock lock(&lock_);
    watch_list_.push_back(mxtl::move(watcher));
    return MX_OK;
}

void WatcherContainer::Notify(const char* name, size_t len, unsigned event) {
    if (len > VFS_WATCH_NAME_MAX) {
        return;
    }

    mxtl::AutoLock lock(&lock_);

    if (watch_list_.is_empty()) {
        return;
    }

    uint8_t msg[sizeof(vfs_watch_msg_t) + len];
    vfs_watch_msg_t* vmsg = reinterpret_cast<vfs_watch_msg_t*>(msg);
    vmsg->event = static_cast<uint8_t>(event);
    vmsg->len = static_cast<uint8_t>(len);
    memcpy(vmsg->name, name, len);

    for (auto it = watch_list_.begin(); it != watch_list_.end();) {
        if (!(it->mask & VFS_WATCH_EVT_MASK(event))) {
            ++it;
            continue;
        }

        mx_status_t status = it->h.write(0, msg, static_cast<uint32_t>(sizeof(msg)), nullptr, 0);
        if (status < 0) {
            // Lazily remove watchers when their handles cannot accept incoming
            // watch messages.
            auto to_remove = it;
            ++it;
            watch_list_.erase(to_remove);
        } else {
            ++it;
        }
    }
}

} // namespace fs
