// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdint.h>

#ifdef __Fuchsia__
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>
#include <stddef.h>
#endif

__BEGIN_CDECLS

#if !defined(__x86_64__)
#error "unsupported architecture"
#endif

// MSRs

#define IPM_MSR_MASK(len, shift) (((1ULL << (len)) - 1) << (shift))

// Bits in the IA32_PERFEVTSELx MSRs.

#define IA32_PERFEVTSEL_EVENT_SELECT_SHIFT (0)
#define IA32_PERFEVTSEL_EVENT_SELECT_LEN   (8)
#define IA32_PERFEVTSEL_EVENT_SELECT_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_EVENT_SELECT_LEN, IA32_PERFEVTSEL_EVENT_SELECT_SHIFT)

#define IA32_PERFEVTSEL_UMASK_SHIFT (8)
#define IA32_PERFEVTSEL_UMASK_LEN   (8)
#define IA32_PERFEVTSEL_UMASK_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_UMASK_LEN, IA32_PERFEVTSEL_UMASK_SHIFT)

#define IA32_PERFEVTSEL_USR_SHIFT (16)
#define IA32_PERFEVTSEL_USR_LEN   (1)
#define IA32_PERFEVTSEL_USR_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_USR_LEN, IA32_PERFEVTSEL_USR_SHIFT)

#define IA32_PERFEVTSEL_OS_SHIFT (17)
#define IA32_PERFEVTSEL_OS_LEN   (1)
#define IA32_PERFEVTSEL_OS_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_OS_LEN, IA32_PERFEVTSEL_OS_SHIFT)

#define IA32_PERFEVTSEL_E_SHIFT (18)
#define IA32_PERFEVTSEL_E_LEN   (1)
#define IA32_PERFEVTSEL_E_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_E_LEN, IA32_PERFEVTSEL_E_SHIFT)

#define IA32_PERFEVTSEL_PC_SHIFT (19)
#define IA32_PERFEVTSEL_PC_LEN   (1)
#define IA32_PERFEVTSEL_PC_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_PC_LEN, IA32_PERFEVTSEL_PC_SHIFT)

#define IA32_PERFEVTSEL_INT_SHIFT (20)
#define IA32_PERFEVTSEL_INT_LEN   (1)
#define IA32_PERFEVTSEL_INT_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_INT_LEN, IA32_PERFEVTSEL_INT_SHIFT)

#define IA32_PERFEVTSEL_ANY_SHIFT (21)
#define IA32_PERFEVTSEL_ANY_LEN   (1)
#define IA32_PERFEVTSEL_ANY_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_ANY_LEN, IA32_PERFEVTSEL_ANY_SHIFT)

#define IA32_PERFEVTSEL_EN_SHIFT (22)
#define IA32_PERFEVTSEL_EN_LEN   (1)
#define IA32_PERFEVTSEL_EN_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_EN_LEN, IA32_PERFEVTSEL_EN_SHIFT)

#define IA32_PERFEVTSEL_INV_SHIFT (23)
#define IA32_PERFEVTSEL_INV_LEN   (1)
#define IA32_PERFEVTSEL_INV_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_INV_LEN, IA32_PERFEVTSEL_INV_SHIFT)

#define IA32_PERFEVTSEL_CMASK_SHIFT (24)
#define IA32_PERFEVTSEL_CMASK_LEN   (8)
#define IA32_PERFEVTSEL_CMASK_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_CMASK_LEN, IA32_PERFEVTSEL_CMASK_SHIFT)

// Bits in the IA32_FIXED_CTR_CTRL MSR.

#define IA32_FIXED_CTR_CTRL_EN_SHIFT(ctr) (0 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_EN_LEN        (2)
#define IA32_FIXED_CTR_CTRL_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_EN_LEN, IA32_FIXED_CTR_CTRL_EN_SHIFT(ctr))

#define IA32_FIXED_CTR_CTRL_ANY_SHIFT(ctr) (2 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_ANY_LEN        (1)
#define IA32_FIXED_CTR_CTRL_ANY_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_ANY_LEN, IA32_FIXED_CTR_CTRL_ANY_SHIFT(ctr))

#define IA32_FIXED_CTR_CTRL_PMI_SHIFT(ctr) (3 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_PMI_LEN        (1)
#define IA32_FIXED_CTR_CTRL_PMI_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_PMI_LEN, IA32_FIXED_CTR_CTRL_PMI_SHIFT(ctr))

// The IA32_PERF_GLOBAL_CTRL MSR.

#define IA32_PERF_GLOBAL_CTRL_PMC_EN_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_CTRL_PMC_EN_LEN        (1)
#define IA32_PERF_GLOBAL_CTRL_PMC_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_CTRL_PMC_EN_LEN, IA32_PERF_GLOBAL_CTRL_PMC_EN_SHIFT(ctr))

#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_LEN        (1)
#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_CTRL_FIXED_EN_LEN, IA32_PERF_GLOBAL_CTRL_FIXED_EN_SHIFT(ctr))

// Bits in the IA32_PERF_GLOBAL_STATUS MSR.
// Note: Use these values for IA32_PERF_GLOBAL_STATUS_RESET and
// IA32_PERF_GLOBAL_STATUS_SET too.

#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_PMC_OVF_LEN, IA32_PERF_GLOBAL_STATUS_PMC_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_FIXED_OVF_LEN, IA32_PERF_GLOBAL_STATUS_FIXED_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_SHIFT (55)
#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_LEN, IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_SHIFT (58)
#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_LBR_FRZ_LEN, IA32_PERF_GLOBAL_STATUS_LBR_FRZ_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_SHIFT (59)
#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_CTR_FRZ_LEN, IA32_PERF_GLOBAL_STATUS_CTR_FRZ_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_ASCI_SHIFT (60)
#define IA32_PERF_GLOBAL_STATUS_ASCI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_ASCI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_ASCI_LEN, IA32_PERF_GLOBAL_STATUS_ASCI_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_SHIFT (61)
#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_LEN, IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_SHIFT (62)
#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_LEN, IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_SHIFT (63)
#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_COND_CHGD_LEN, IA32_PERF_GLOBAL_STATUS_COND_CHGD_SHIFT)

// Bits in the IA32_PERF_GLOBAL_INUSE MSR.

#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_SHIFT (63)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_PMI_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_PMI_SHIFT)

// Bits in the IA32_PERF_GLOBAL_OVF_CTRL MSR.

#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_SHIFT(ctr) (0)
#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_SHIFT (61)
#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_SHIFT)

#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_SHIFT (62)
#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_SHIFT)

#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_SHIFT (63)
#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_LEN, IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_SHIFT)

// Bits in the IA32_DEBUGCTL MSR.

#define IA32_DEBUGCTL_LBR_SHIFT (0)
#define IA32_DEBUGCTL_LBR_LEN   (1)
#define IA32_DEBUGCTL_LBR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_LBR_LEN, IA32_DEBUGCTL_LBR_SHIFT)

#define IA32_DEBUGCTL_BTF_SHIFT (1)
#define IA32_DEBUGCTL_BTF_LEN   (1)
#define IA32_DEBUGCTL_BTF_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTF_LEN, IA32_DEBUGCTL_BTF_SHIFT)

#define IA32_DEBUGCTL_TR_SHIFT (6)
#define IA32_DEBUGCTL_TR_LEN   (1)
#define IA32_DEBUGCTL_TR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_TR_LEN, IA32_DEBUGCTL_TR_SHIFT)

#define IA32_DEBUGCTL_BTS_SHIFT (7)
#define IA32_DEBUGCTL_BTS_LEN   (1)
#define IA32_DEBUGCTL_BTS_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_LEN, IA32_DEBUGCTL_BTS_SHIFT)

#define IA32_DEBUGCTL_BTINT_SHIFT (8)
#define IA32_DEBUGCTL_BTINT_LEN   (1)
#define IA32_DEBUGCTL_BTINT_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTINT_LEN, IA32_DEBUGCTL_BTINT_SHIFT)

#define IA32_DEBUGCTL_BTS_OFF_OS_SHIFT (9)
#define IA32_DEBUGCTL_BTS_OFF_OS_LEN   (1)
#define IA32_DEBUGCTL_BTS_OFF_OS_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_OFF_OS_LEN, IA32_DEBUGCTL_BTS_OFF_OS_SHIFT)

#define IA32_DEBUGCTL_BTS_OFF_USR_SHIFT (10)
#define IA32_DEBUGCTL_BTS_OFF_USR_LEN   (1)
#define IA32_DEBUGCTL_BTS_OFF_USR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_OFF_USR_LEN, IA32_DEBUGCTL_BTS_OFF_USR_SHIFT)

#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_SHIFT (11)
#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_LEN, IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_SHIFT)

#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_SHIFT (12)
#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_LEN, IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_SHIFT)

#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_SHIFT (14)
#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_LEN, IA32_DEBUGCTL_FREEZE_WHILE_SMM_EN_SHIFT)

#define IA32_DEBUGCTL_RTM_SHIFT (15)
#define IA32_DEBUGCTL_RTM_LEN   (1)
#define IA32_DEBUGCTL_RTM_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_RTM_LEN, IA32_DEBUGCTL_RTM_SHIFT)

// maximum number of programmable counters
#define IPM_MAX_PROGRAMMABLE_COUNTERS 8

// maximum number of fixed-use counters
#define IPM_MAX_FIXED_COUNTERS 3

// API version number (useful when doing incompatible upgrades)
#define IPM_API_VERSION 2

// Buffer format version
#define IPM_BUFFER_COUNTING_MODE_VERSION 0
#define IPM_BUFFER_SAMPLING_MODE_VERSION 0

// The HW PERF pseudo register sets.
// These are accessed via mtrace for now.

// Current state of data collection.
typedef struct {
    // S/W API version (some future proofing, always zero for now).
    uint32_t api_version;
    // The H/W Performance Monitor version.
    uint32_t pm_version;
    // The number of fixed counters.
    uint32_t num_fixed_counters;
    // The number of programmable counters.
    uint32_t num_programmable_counters;
    // The PERF_CAPABILITIES MSR.
    uint64_t perf_capabilities;
    // True if MTRACE_IPM_ALLOC done.
    bool alloced;
    // True if MTRACE_IPM_START done.
    bool started;
} zx_x86_ipm_properties_t;

// This is for passing buffer specs to the kernel (for setting up the
// debug store MSRs, or for directly writing in "counting mode").
typedef struct {
    zx_handle_t vmo;
} zx_x86_ipm_buffer_t;

typedef struct {
    // IA32_PERF_GLOBAL_CTRL
    uint64_t global_ctrl;

    // IA32_PERFEVTSEL_*
    uint64_t programmable_events[IPM_MAX_PROGRAMMABLE_COUNTERS];

    // IA32_FIXED_CTR_CTRL
    uint64_t fixed_ctrl;

    // IA32_DEBUGCTL
    uint64_t debug_ctrl;

    // IPM_MISC_CTRL_*
    // These are not part of IPM but are additional data we can collect.
    uint32_t misc_ctrl;
#define IPM_MISC_CTRL_MASK       0x1
// Collect aspace+pc values.
#define IPM_MISC_CTRL_PROFILE_PC 0x1

    // Sampling frequency. If zero then do simple counting (collect a tally
    // of all counts and report at the end).
    // When a counter gets this many hits an interrupt is generated.
    uint32_t sample_freq;

    // TODO(dje): Add initial counter values here instead of always resetting
    // to zero?
} zx_x86_ipm_config_t;

// Header for each data buffer.
typedef struct {
    // Format version number (some future proofing, always zero for now).
    uint32_t version;
    uint32_t padding;
    uint64_t ticks_per_second;
    uint64_t capture_end;
} zx_x86_ipm_buffer_info_t;

// This is the format of the data in the trace buffer for "counting mode".
typedef struct {
    // IA32_PERF_GLOBAL_STATUS
    uint64_t status;

    zx_time_t time;

    // IA32_PMC_*
    uint64_t programmable_counters[IPM_MAX_PROGRAMMABLE_COUNTERS];

    // IA32_FIXED_CTR*
    uint64_t fixed_counters[IPM_MAX_FIXED_COUNTERS];
} zx_x86_ipm_counters_t;

// The type of "sampling mode" record.
typedef enum {
  // Reserved, unused.
  IPM_RECORD_RESERVED = 0,
  // The record is an |zx_x86_ipm_tick_record_t|.
  IPM_RECORD_TICK = 1,
  // The record is an |zx_x86_ipm_value_record_t|.
  IPM_RECORD_VALUE = 2,
  // The record is an |zx_x86_ipm_pc_record_t|.
  IPM_RECORD_PC = 3,
} zx_x86_ipm_record_type_t;

// Sampling mode data header.
typedef struct {
    uint8_t type;

    // A possible usage of this field is to add some type-specific flags.
    uint8_t reserved_flags;

    uint16_t counter;
// OR'd to the value in |counter| to indicate a fixed counter.
#define IPM_COUNTER_NUMBER_FIXED 0x100

    // TODO(dje): Remove when |time| becomes 32 bits.
    uint32_t reserved;

    // TODO(dje): Reduce this to 32 bits (e.g., by adding clock records to
    // the buffer).
    zx_time_t time;
} zx_x86_ipm_record_header_t;

// Record the time a counter was sampled.
// This does not include the counter value in order to keep the size small.
// This is for use when the counter is its own trigger so the user should know
// what value was: it's the sample frequency.
typedef struct {
    zx_x86_ipm_record_header_t header;
} zx_x86_ipm_tick_record_t;

// Record the value of a counter at a particular time.
// This is used when another timebase is driving the sampling, e.g., another
// counter.
typedef struct {
    zx_x86_ipm_record_header_t header;
    uint64_t value;
} zx_x86_ipm_value_record_t;

// Record the aspace+pc values.
// This is used when doing gprof-like profiling.
// There is no point in recording the counter's value here as the counter
// must be its own trigger.
typedef struct {
    zx_x86_ipm_record_header_t header;
    // In the case of x86 this is the cr3 value.
    uint64_t aspace;
    uint64_t pc;
} zx_x86_ipm_pc_record_t;

///////////////////////////////////////////////////////////////////////////////

// Flags for the counters in *-pm-events.inc.
// See for example Intel Volume 3, Table 19-3.
// "Non-Architectural Performance Events of the Processor Core Supported by
// Skylake Microarchitecture and Kaby Lake Microarchitecture"

// Flags for non-architectural counters
// CounterMask values
#define IPM_REG_FLAG_CMSK_MASK 0xff
#define IPM_REG_FLAG_CMSK1   1
#define IPM_REG_FLAG_CMSK2   2
#define IPM_REG_FLAG_CMSK4   4
#define IPM_REG_FLAG_CMSK5   5
#define IPM_REG_FLAG_CMSK6   6
#define IPM_REG_FLAG_CMSK8   8
#define IPM_REG_FLAG_CMSK10 10
#define IPM_REG_FLAG_CMSK12 12
#define IPM_REG_FLAG_CMSK16 16
#define IPM_REG_FLAG_CMSK20 20
// AnyThread = 1 required
#define IPM_REG_FLAG_ANYT      0x100
// Invert = 1 required
#define IPM_REG_FLAG_INV       0x200
// Edge = 1 required
#define IPM_REG_FLAG_EDG       0x400
// Also supports PEBS and DataLA
#define IPM_REG_FLAG_PSDLA     0x800
// Also supports PEBS
#define IPM_REG_FLAG_PS        0x1000

// Extra flags
// Architectural event
#define IPM_REG_FLAG_ARCH      0x10000
// Fixed counters
#define IPM_REG_FLAG_FIXED0    0x100000
#define IPM_REG_FLAG_FIXED1    0x200000
#define IPM_REG_FLAG_FIXED2    0x400000

///////////////////////////////////////////////////////////////////////////////

#ifdef __Fuchsia__

// ioctls

// Fetch the state of data collection.
// Must be called prior to STAGE_CPU_DATA and after any intermediate FREE.
// Output: zx_x86_ipm_properties_t
#define IOCTL_IPM_GET_PROPERTIES \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 0)
IOCTL_WRAPPER_OUT(ioctl_ipm_get_properties, IOCTL_IPM_GET_PROPERTIES,
                  zx_x86_ipm_properties_t);

// The configuration for a data collection run.
// This is generally the first call to allocate resources for a trace,
// "trace" is used generically here: == "data collection run".
// TODO(dje): At the moment we only support one active trace. Will relax in
// time once things are working (e.g., so different data collections can be
// going on at the same time for, say, different processes or jobs).
typedef struct {
    uint32_t num_buffers; // must be #cpus for now
    uint32_t buffer_size;

    // TODO(dje): Later provide ability to request other resources needed
    // for the trace. For now, give client access to full data collection
    // capabilities provided by h/w.
    // Also provide ability to specify "trace entire system" vs "trace this
    // process/job". Maybe even just a particular cpu - dunno.
} ioctl_ipm_trace_config_t;

// Create a trace, allocating the needed trace buffers and other resources.
// Think open(O_CREAT|...) of a file.
// For "counting mode" this is just a page per cpu to hold resulting
// counter values. TODO(dje): constrain buffer_size.
// For "sampling mode" this is #cpus buffers each of size buffer_size.
// "other resources" is basically a catch-all for other things that will
// be needed.
// TODO(dje): Return a descriptor for the trace so that different clients
// can make different requests and potentially have them all be active
// (e.g., different traces for different processes/jobs, assuming various
// factors like them being sufficiently compatible for whatever definition
// of "compatible" ultimately arises).
// Input: ioctl_ipm_trace_config_t
#define IOCTL_IPM_ALLOC_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 1)
IOCTL_WRAPPER_IN(ioctl_ipm_alloc_trace, IOCTL_IPM_ALLOC_TRACE,
                 ioctl_ipm_trace_config_t);

// Free all trace buffers and any other resources allocated for the trace.
// Should be the last thing called (e.g., think close() of an fd).
// TODO(dje): See IOCTL_IPM_ALLOC_TRACE.
#define IOCTL_IPM_FREE_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 2)
IOCTL_WRAPPER(ioctl_ipm_free_trace, IOCTL_IPM_FREE_TRACE);

// Return config data for a trace buffer.
// Output: ioctl_ipm_trace_config_t
#define IOCTL_IPM_GET_TRACE_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 3)
IOCTL_WRAPPER_OUT(ioctl_ipm_get_trace_config, IOCTL_IPM_GET_TRACE_CONFIG,
                  ioctl_ipm_trace_config_t);

// Full-featured perf-data trace configuration.
typedef struct {
    zx_x86_ipm_config_t config;
} ioctl_ipm_config_t;

// Stage performance monitor configuration for a cpu.
// Must be called with data collection off and after INIT.
// Note: This doesn't actually configure the counters, this just stages
// the values for subsequent use by START.
// Input: ioctl_ipm_config_t
// TODO(dje): Provide a more abstract way to configure the hardware.
#define IOCTL_IPM_STAGE_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 4)
IOCTL_WRAPPER_IN(ioctl_ipm_stage_config, IOCTL_IPM_STAGE_CONFIG,
                 ioctl_ipm_config_t);

// Fetch performance monitor configuration for a cpu.
// Must be called with data collection off and after INIT.
// Output: ioctl_ipm_config_t
#define IOCTL_IPM_GET_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 6)
IOCTL_WRAPPER_OUT(ioctl_ipm_get_config, IOCTL_IPM_GET_CONFIG,
                  ioctl_ipm_config_t);

// This contains the run-time produced data about the buffer.
// Not the trace data itself, just info about the data.
typedef struct {
    // Offset in the buffer where tracing stopped.
    uint64_t capture_end;
} ioctl_ipm_buffer_info_t;

// Get trace data associated with the buffer.
// Input: trace buffer descriptor (0, 1, 2, ..., |num_buffers|-1)
// Output: ioctl_ipm_buffer_info_t
#define IOCTL_IPM_GET_BUFFER_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 7)
IOCTL_WRAPPER_INOUT(ioctl_ipm_get_buffer_info, IOCTL_IPM_GET_BUFFER_INFO,
                    uint32_t, ioctl_ipm_buffer_info_t);

typedef struct {
    uint32_t descriptor;
} ioctl_ipm_buffer_handle_req_t;

// Return a handle of a trace buffer.
// There is no API to get N handles, we have to get them one at a time.
// [There's no point in trying to micro-optimize this and, say, get 3 at
// a time.]
// Input: trace buffer descriptor (0, 1, 2, ..., |num_buffers|-1)
// Output: handle of the vmo of the buffer
#define IOCTL_IPM_GET_BUFFER_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_IPM, 8)
IOCTL_WRAPPER_INOUT(ioctl_ipm_get_buffer_handle, IOCTL_IPM_GET_BUFFER_HANDLE,
                    ioctl_ipm_buffer_handle_req_t, zx_handle_t);

// Turn on data collection.
// Must be called after INIT and with data collection off.
#define IOCTL_IPM_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 10)
IOCTL_WRAPPER(ioctl_ipm_start, IOCTL_IPM_START);

// Turn off data collection.
// May be called before INIT.
// May be called multiple times.
#define IOCTL_IPM_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_IPM, 11)
IOCTL_WRAPPER(ioctl_ipm_stop, IOCTL_IPM_STOP);

#endif // __Fuchsia__

__END_CDECLS
