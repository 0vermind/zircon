// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/devmgr.h>

#include <magenta/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <mxio/io.h>

#include "devhost.h"

static ssize_t dmctl_write(mx_device_t* dev, const void* buf, size_t count, mx_off_t off) {
    char cmd[1024];
    if (count < sizeof(cmd)) {
        memcpy(cmd, buf, count);
        cmd[count] = 0;
    } else {
        return ERR_INVALID_ARGS;
    }
    return devmgr_control(cmd);
}

static mx_protocol_device_t dmctl_device_proto = {
    .write = dmctl_write,
};

mx_handle_t _dmctl_handle = MX_HANDLE_INVALID;

mx_status_t dmctl_init(mx_driver_t* driver) {
    mx_device_t* dev;
    mx_status_t s = device_create(&dev, driver, "dmctl", &dmctl_device_proto);
    if (s != NO_ERROR) {
        return s;
    }
    s = device_add(dev, driver_get_misc_device());
    if (s != NO_ERROR) {
        free(dev);
        return s;
    }
    _dmctl_handle = dev->rpc;
    return NO_ERROR;
}

mx_driver_t _driver_dmctl = {
    .name = "dmctl",
    .ops = {
        .init = dmctl_init,
    },
};
