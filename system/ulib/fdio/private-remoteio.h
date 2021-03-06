// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zxio/inception.h>

#include "private.h"

typedef struct zxrio zxrio_t;
struct zxrio {
    // base fdio io object
    fdio_t io;

    // channel handle for rpc
    zx_handle_t h;

    // event handle for device state signals
    zx_handle_t event;
};

// Implements the |fdio_t| contract using |zxio_remote_t|.
//
// Has an ops table that translates fdio ops into zxio ops. Some of the fdio ops
// require using the underlying handles in the |zxio_remote_t|, which is why
// this object needs to use |zxio_remote_t| directly.
//
// Will be removed once the transition to the zxio backend is complete.
typedef struct fdio_zxio_remote {
    fdio_t io;
    zxio_remote_t remote;
} fdio_zxio_remote_t;

// Create an |fdio_t| for a remote file backed by zxio.
fdio_t* fdio_zxio_create_remote(zx_handle_t control, zx_handle_t event);

// open operation directly on remoteio handle
zx_status_t zxrio_open_handle(zx_handle_t h, const char* path, uint32_t flags,
                              uint32_t mode, fdio_t** out);

extern fdio_ops_t zx_remote_ops;
extern fdio_ops_t fdio_zxio_remote_ops;
