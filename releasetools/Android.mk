# Copyright (C) 2012 The Android Open Source Project
# Copyright (C) 2012 Havlena Petr <havlenapetr@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifneq ($(filter galaxys,$(TARGET_DEVICE)),)

LOCAL_PATH:= $(call my-dir)

# Releasetools
TARGET_RELEASETOOLS_EXTENSIONS := $(LOCAL_PATH)

include $(CLEAR_VARS)
LOCAL_MODULE := modem.bin
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/firmware
LOCAL_SRC_FILES := ../../../../vendor/samsung/$(TARGET_DEVICE)/proprietary/modem.bin
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := updater.sh
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_SRC_FILES := updater.sh
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := bml_over_mtd
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_SRC_FILES := bml_over_mtd
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)
LOCAL_MODULE := bml_over_mtd.sh
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_SRC_FILES := bml_over_mtd.sh
include $(BUILD_PREBUILT)

#include $(CLEAR_VARS)
#LOCAL_MODULE := mksecbootimg
#LOCAL_MODULE_TAGS := eng
#LOCAL_MODULE_CLASS := EXECUTABLES
#LOCAL_SRC_FILES := mksecbootimg
#include $(BUILD_HOST_PREBUILT)

endif
