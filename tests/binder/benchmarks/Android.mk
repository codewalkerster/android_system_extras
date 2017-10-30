#
# Copyright (C) 2010 The Android Open Source Project
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
#

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_MODULE_TAGS := eng tests

LOCAL_STATIC_LIBRARIES += \
    libtestUtil

LOCAL_SHARED_LIBRARIES += \
    libutils \
    liblog \
    libbinder

LOCAL_C_INCLUDES += \
    system/extras/tests/include \
    frameworks/base/include

LOCAL_MODULE := binderAddInts
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_SRC_FILES := binderAddInts.cpp
LOCAL_CFLAGS:= -Wall -Werror

include $(BUILD_NATIVE_BENCHMARK)
