// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/protocol/platform-defs.h>
#include <hw/reg.h>

#include <soc/aml-common/aml-usb-phy-v2.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "aml.h"

#define BIT_MASK(start, count) (((1 << (count)) - 1) << (start))
#define SET_BITS(dest, start, count, value) \
        ((dest & ~BIT_MASK(start, count)) | (((value) << (start)) & BIT_MASK(start, count)))

static const pbus_mmio_t sdio_mmios[] = {
    {
        .base = S905D2_EMMC_A_SDIO_BASE,
        .length = S905D2_EMMC_A_SDIO_LENGTH,
    },

    {
        .base = S905D2_CBUS_BASE,
        .length = S905D2_CBUS_LENGTH,
    }
};

static const pbus_irq_t sdio_irqs[] = {
    {
        .irq = S905D2_EMMC_A_SDIO_IRQ,
    },
};

static const pbus_bti_t sdio_btis[] = {
    {
        .iommu_index = 0,
        .bti_id = BTI_SDIO,
    },
};

static const pbus_gpio_t sdio_gpios[] = {
    {
        .gpio = S905D2_GPIOX(6),
    },
};

static const pbus_dev_t sdio_dev = {
    .name = "aml_sdio",
    .vid = PDEV_VID_AMLOGIC,
    .pid = PDEV_PID_GENERIC,
    .did = PDEV_DID_AMLOGIC_SD_EMMC,
    .mmios = sdio_mmios,
    .mmio_count = countof(sdio_mmios),
    .irqs = sdio_irqs,
    .irq_count = countof(sdio_irqs),
    .btis = sdio_btis,
    .bti_count = countof(sdio_btis),
    .gpios = sdio_gpios,
    .gpio_count = countof(sdio_gpios),

};

zx_status_t aml_sdio_init(aml_bus_t* bus) {
    zx_status_t status;

    // set alternate functions to enable EMMC
    zxlogf(INFO, "aml_sdio_init START\n");
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D0, S905D2_WIFI_SDIO_D0_FN);
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D1, S905D2_WIFI_SDIO_D1_FN);
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D2, S905D2_WIFI_SDIO_D2_FN);
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_D3, S905D2_WIFI_SDIO_D3_FN);
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CLK, S905D2_WIFI_SDIO_CLK_FN);
    gpio_set_alt_function(&bus->gpio, S905D2_WIFI_SDIO_CMD, S905D2_WIFI_SDIO_CMD_FN);

    if ((status = pbus_device_add(&bus->pbus, &sdio_dev, 0)) != ZX_OK) {
        zxlogf(ERROR, "aml_sdio_init could not add sdio_dev: %d\n", status);
        return status;
    }

    return ZX_OK;
}
