// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/iommu.h>
#include <ddk/protocol/platform-bus.h>

// BTI IDs for our devices
enum {
    BTI_BOARD,
    BTI_USB1,
    BTI_USB2,
    BTI_DISPLAY,
    BTI_GPU,
    BTI_SDHCI,
};

typedef enum {
    BOARD_IMX8M_EVK,
    BOARD_MADRONE,
} imx8_board_t;

typedef struct {
    platform_bus_protocol_t     pbus;
    zx_device_t*                parent;
    iommu_protocol_t            iommu;
    gpio_protocol_t             gpio;
    zx_handle_t                 bti_handle;
    imx8_board_t                board;
} imx8mevk_bus_t;

zx_status_t imx8m_gpio_init(imx8mevk_bus_t* bus);
zx_status_t imx_usb_phy_init(zx_paddr_t usb_base, size_t usb_length, zx_handle_t bti);
zx_status_t imx_usb_init(imx8mevk_bus_t* bus);
zx_status_t madrone_usb_init(imx8mevk_bus_t* bus);
zx_status_t imx_gpu_init(imx8mevk_bus_t* bus);
zx_status_t imx8m_sdhci_init(imx8mevk_bus_t* bus);