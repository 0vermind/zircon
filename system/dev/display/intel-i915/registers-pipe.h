// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/protocol/display-controller.h>
#include <ddk/protocol/intel-gpu-core.h>
#include <hwreg/bitfields.h>
#include <zircon/pixelformat.h>

namespace registers {

// Number of pipes that the hardware provides.
static constexpr uint32_t kPipeCount = 3;

enum Pipe { PIPE_A, PIPE_B, PIPE_C };

static const Pipe kPipes[kPipeCount] = {
    PIPE_A, PIPE_B, PIPE_C,
};

static constexpr uint32_t kPrimaryPlaneCount = 3;

// PIPE_SRCSZ
class PipeSourceSize : public hwreg::RegisterBase<PipeSourceSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x6001c;

    DEF_FIELD(28, 16, horizontal_source_size);
    DEF_FIELD(11, 0, vertical_source_size);
};


// PLANE_SURF
class PlaneSurface : public hwreg::RegisterBase<PlaneSurface, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7019c;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_addr);
    static constexpr uint32_t kRShiftCount = 12;
    static constexpr uint32_t kLinearAlignment = 256 * 1024;
    static constexpr uint32_t kXTilingAlignment = 256 * 1024;
    static constexpr uint32_t kYTilingAlignment = 1024 * 1024;
    static constexpr uint32_t kTrailingPtePadding = 136;
    static constexpr uint32_t kHeaderPtePaddingFor180Or270 = 136;

    DEF_BIT(3, ring_flip_source);
};

// PLANE_SURFLIVE
class PlaneSurfaceLive : public hwreg::RegisterBase<PlaneSurfaceLive, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x701ac;

    // This field omits the lower 12 bits of the address, so the address
    // must be 4k-aligned.
    static constexpr uint32_t kPageShift = 12;
    DEF_FIELD(31, 12, surface_base_addr);
};

// PLANE_STRIDE
class PlaneSurfaceStride : public hwreg::RegisterBase<PlaneSurfaceStride, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70188;

    DEF_FIELD(9, 0, stride);

    void set_stride(uint32_t tiling, uint32_t width, zx_pixel_format_t format) {
        uint32_t chunk_size = get_chunk_size(tiling, format);
        set_stride(fbl::round_up(width * ZX_PIXEL_FORMAT_BYTES(format), chunk_size) / chunk_size);
    }

    static uint32_t compute_pixel_stride(uint32_t tiling, uint32_t width,
                                         zx_pixel_format_t format) {
        uint32_t chunk_size = get_chunk_size(tiling, format);
        return fbl::round_up(width, chunk_size / ZX_PIXEL_FORMAT_BYTES(format));
    }

private:
    static uint32_t get_chunk_size(uint32_t tiling, zx_pixel_format_t format) {
        switch (tiling) {
        case IMAGE_TYPE_SIMPLE: return 64;
        case IMAGE_TYPE_X_TILED: return 512;
        case IMAGE_TYPE_Y_LEGACY_TILED: return 128;
        case IMAGE_TYPE_YF_TILED: return ZX_PIXEL_FORMAT_BYTES(format) == 1 ? 64 : 128;
        default:
            ZX_ASSERT(false);
            return 0;
        }
    }
};

// PLANE_SIZE
class PlaneSurfaceSize : public hwreg::RegisterBase<PlaneSurfaceSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70190;

    DEF_FIELD(27, 16, height_minus_1);
    DEF_FIELD(12, 0, width_minus_1);
};

// PLANE_CTL
class PlaneControl : public hwreg::RegisterBase<PlaneControl, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70180;

    DEF_BIT(31, plane_enable);
    DEF_BIT(30, pipe_gamma_enable);
    DEF_BIT(29, remove_yuv_offset);
    DEF_BIT(28, yuv_range_correction_disable);

    DEF_FIELD(27, 24, source_pixel_format);
    static constexpr uint32_t kFormatRgb8888 = 4;

    DEF_BIT(23, pipe_csc_enable);
    DEF_FIELD(22, 21, key_enable);
    DEF_BIT(20, rgb_color_order);
    DEF_BIT(19, plane_yuv_to_rgb_csc_dis);
    DEF_BIT(18, plane_yuv_to_rgb_csc_format);
    DEF_FIELD(17, 16, yuv_422_byte_order);
    DEF_BIT(15, render_decompression);
    DEF_BIT(14, trickle_feed_enable);
    DEF_BIT(13, plane_gamma_disable);

    DEF_FIELD(12, 10, tiled_surface);
    static constexpr uint32_t kLinear = 0;
    static constexpr uint32_t kTilingX = 1;
    static constexpr uint32_t kTilingYLegacy = 4;
    static constexpr uint32_t kTilingYF = 5;

    DEF_BIT(9, async_address_update_enable);
    DEF_FIELD(7, 6, stereo_surface_vblank_mask);
    DEF_FIELD(5, 4, alpha_mode);
    DEF_BIT(3, allow_double_buffer_update_disable);
    DEF_FIELD(1, 0, plane_rotation);
};

// PLANE_BUF_CFG
class PlaneBufCfg : public hwreg::RegisterBase<PlaneBufCfg, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7017c;

    DEF_FIELD(25, 16, buffer_end);
    DEF_FIELD(9, 0, buffer_start);
};

// PLANE_WM
class PlaneWm : public hwreg::RegisterBase<PlaneWm, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x70140;

    DEF_BIT(31, enable);
    DEF_FIELD(18, 14, lines);
    DEF_FIELD(9, 0, blocks);
};

// PS_CTRL
class PipeScalerCtrl : public hwreg::RegisterBase<PipeScalerCtrl, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x68180;

    DEF_BIT(31, enable);
};

// PS_WIN_SIZE
class PipeScalerWinSize : public hwreg::RegisterBase<PipeScalerWinSize, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x68174;

    DEF_FIELD(28, 16, x_size);
    DEF_FIELD(11, 0, y_size);
};

// DE_PIPE_INTERRUPT
class PipeDeInterrupt : public hwreg::RegisterBase<PipeDeInterrupt, uint32_t> {
public:
    DEF_BIT(1, vsync);
};

// PLANE_OFFSET
class PlaneOffset : public hwreg::RegisterBase<PlaneOffset, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x701a4;

    DEF_FIELD(28, 16, start_y);
    DEF_FIELD(12, 0, start_x);
};

// PLANE_POS
class PlanePosition : public hwreg::RegisterBase<PlanePosition, uint32_t> {
public:
    static constexpr uint32_t kBaseAddr = 0x7018c;

    DEF_FIELD(28, 16, y_pos);
    DEF_FIELD(12, 0, x_pos);
};

// An instance of PipeRegs represents the registers for a particular pipe.
class PipeRegs {
public:
    static constexpr uint32_t kStatusReg = 0x44400;
    static constexpr uint32_t kMaskReg = 0x44404;
    static constexpr uint32_t kIdentityReg = 0x44408;
    static constexpr uint32_t kEnableReg = 0x4440c;

    PipeRegs(Pipe pipe) : pipe_(pipe) { }

    hwreg::RegisterAddr<registers::PipeSourceSize> PipeSourceSize() {
        return GetReg<registers::PipeSourceSize>();
    }

    hwreg::RegisterAddr<registers::PlaneSurface> PlaneSurface(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurface>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceLive> PlaneSurfaceLive(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceLive>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceStride> PlaneSurfaceStride(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceStride>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneSurfaceSize> PlaneSurfaceSize(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneSurfaceSize>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneControl> PlaneControl(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneControl>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlaneOffset> PlaneOffset(int32_t plane_num) {
        return GetPlaneReg<registers::PlaneOffset>(plane_num);
    }
    hwreg::RegisterAddr<registers::PlanePosition> PlanePosition(int32_t plane_num) {
        return GetPlaneReg<registers::PlanePosition>(plane_num);
    }
    // 0 == cursor, 1-3 are regular planes
    hwreg::RegisterAddr<registers::PlaneBufCfg> PlaneBufCfg(int plane) {
        return hwreg::RegisterAddr<registers::PlaneBufCfg>(
                PlaneBufCfg::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    hwreg::RegisterAddr<registers::PlaneWm>PlaneWatermark(int plane, int wm_num) {
        return hwreg::RegisterAddr<PlaneWm>(
                PlaneWm::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane + 4 * wm_num);
    }

    hwreg::RegisterAddr<registers::PipeScalerCtrl> PipeScalerCtrl(int num) {
        return hwreg::RegisterAddr<registers::PipeScalerCtrl>(
                PipeScalerCtrl::kBaseAddr + 0x800 * pipe_ + num * 0x100);
    }

    hwreg::RegisterAddr<registers::PipeScalerWinSize> PipeScalerWinSize(int num) {
        return hwreg::RegisterAddr<registers::PipeScalerWinSize>(
                PipeScalerWinSize::kBaseAddr + 0x800 * pipe_ + num * 0x100);
    }

    hwreg::RegisterAddr<registers::PipeDeInterrupt> PipeDeInterrupt(uint32_t type) {
        return hwreg::RegisterAddr<registers::PipeDeInterrupt>(type + 0x10 * pipe_);
    }

private:
    template <class RegType> hwreg::RegisterAddr<RegType> GetReg() {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_);
    }

    template <class RegType> hwreg::RegisterAddr<RegType> GetPlaneReg(int32_t plane) {
        return hwreg::RegisterAddr<RegType>(RegType::kBaseAddr + 0x1000 * pipe_ + 0x100 * plane);
    }

    Pipe pipe_;
};

} // namespace registers
