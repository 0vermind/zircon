# Copyright 2017 The Fuchsia Authors
#
# Use of this source code is governed by a MIT-style
# license that can be found in the LICENSE file or at
# https://opensource.org/licenses/MIT

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS := \
	$(LOCAL_DIR)/guest_physical_address_space.cpp \
	$(LOCAL_DIR)/fault.cpp \
	$(LOCAL_DIR)/hypervisor_unittest.cpp \
	$(LOCAL_DIR)/packet_mux.cpp \

MODULE_DEPS := \
    kernel/lib/mxtl \
    kernel/lib/unittest \

include make/module.mk
