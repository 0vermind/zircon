// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sherlock.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/algorithm.h>
#include <fbl/unique_ptr.h>

namespace sherlock {

zx_status_t Sherlock::Create(zx_device_t* parent) {
    platform_bus_protocol_t pbus;

    auto status = device_get_protocol(parent, ZX_PROTOCOL_PLATFORM_BUS, &pbus);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto board = fbl::make_unique_checked<Sherlock>(&ac, parent, &pbus);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = board->DdkAdd("sherlock", DEVICE_ADD_NON_BINDABLE);
    if (status != ZX_OK) {
        return status;
    }

    // Start up our protocol helpers and platform devices.
    status = board->Start();
    if (status == ZX_OK) {
        // devmgr is now in charge of the device.
        __UNUSED auto* dummy = board.release();
    }
    return status;
}

int Sherlock::Thread() {
    if (GpioInit() != ZX_OK) {
        zxlogf(ERROR, "GpioInit() failed\n");
        return -1;
    }
    return 0;
}

zx_status_t Sherlock::Start() {
    int rc = thrd_create_with_name(&thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Sherlock*>(arg)->Thread();
                                   },
                                   this,
                                   "sherlock-start-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }
    return ZX_OK;
}

void Sherlock::DdkRelease() {
    delete this;
}

} // namespace sherlock

zx_status_t sherlock_bind(void* ctx, zx_device_t* parent) {
    return sherlock::Sherlock::Create(parent);
}
