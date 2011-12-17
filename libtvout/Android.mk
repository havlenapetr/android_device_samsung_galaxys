#########################################################################
# Build tvout driver library
#########################################################################

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS:=-fno-short-enums

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include

LOCAL_SRC_FILES:= \
    fimc_lib.c \
    fimd_api.c \
    SecTv.cpp

LOCAL_SHARED_LIBRARIES:= \
    libutils \
    liblog \
    libbinder \
    libcutils

LOCAL_MODULE := libsectvout
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

#########################################################################
# Build driver test binary
#########################################################################

include $(CLEAR_VARS)

LOCAL_CFLAGS:=-fno-short-enums

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include

LOCAL_SRC_FILES:= \
    test.cpp

LOCAL_SHARED_LIBRARIES:= \
    libsectvout

LOCAL_MODULE := tvout_test
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)

#include $(call all-makefiles-under,$(LOCAL_PATH))