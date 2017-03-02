// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits.h>
#include <stdbool.h>
#include <unistd.h> // for ssize_t

#include <magenta/types.h>
#include <magenta/compiler.h>

#include <sys/epoll.h>

#define MAX_MXIO_FD 256

// flag on handle args in processargs
// instructing that this fd should be dup'd to 0/1/2
// and be used for all of stdio
#define MXIO_FLAG_USE_FOR_STDIO 0x8000

#define MXIO_NONBLOCKING 1

#define MXIO_PROTOCOL_UNDEFINED 0
#define MXIO_PROTOCOL_PIPE 1
#define MXIO_PROTOCOL_REMOTE 2
#define MXIO_PROTOCOL_VMOFILE 3
#define MXIO_PROTOCOL_SOCKET 4

// maximum handles used in open/clone/create
#define MXIO_MAX_HANDLES 3

// mxio_ops_t's read/write are able to do io of
// at least this size
#define MXIO_CHUNK_SIZE 8192

// Maxium size for an ioctl input
#define MXIO_IOCTL_MAX_INPUT 1024

// Maxium length of a filename
#define MXIO_MAX_FILENAME NAME_MAX

// events for mxio_wait_fd()
#define MXIO_EVT_READABLE EPOLLIN
#define MXIO_EVT_WRITABLE EPOLLOUT
#define MXIO_EVT_ERROR EPOLLERR
#define MXIO_EVT_ALL (EPOLLIN | EPOLLOUT | EPOLLERR)

__BEGIN_CDECLS

// wait until one or more events are pending
mx_status_t mxio_wait_fd(int fd, uint32_t events, uint32_t* pending, mx_time_t timeout);

// create a fd that works with wait APIs (epoll, select, etc.) from a handle
// and expected signals (signals_in/signals_out correspond to EPOLLIN/EPOLLOUT
// events respectively). the handle will be closed when the fd is closed, unless
// shared_handle is true.
int mxio_handle_fd(mx_handle_t h, mx_signals_t signals_in, mx_signals_t signals_out, bool shared_handle);

// invoke a raw mxio ioctl
ssize_t mxio_ioctl(int fd, int op, const void* in_buf, size_t in_len, void* out_buf, size_t out_len);

// create a pipe, installing one half in a fd, returning the other
// for transport to another process
mx_status_t mxio_pipe_half(mx_handle_t* handle, uint32_t* type);

__END_CDECLS
