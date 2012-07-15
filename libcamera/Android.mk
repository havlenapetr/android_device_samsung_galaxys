ifneq ($(filter galaxys,$(TARGET_DEVICE)),)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS:=-fno-short-enums

LOCAL_C_INCLUDES += $(LOCAL_PATH)/../include
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../../crespo/libs3cjpeg
LOCAL_C_INCLUDES += frameworks/native/include/media/hardware

LOCAL_SRC_FILES:= \
    hal_module.cpp \
    SecCamera.cpp \
    SecCameraParameters.cpp \
    SecCameraHWInterface.cpp

LOCAL_SHARED_LIBRARIES:= libutils libui liblog libbinder libcutils
LOCAL_SHARED_LIBRARIES+= libs3cjpeg
LOCAL_SHARED_LIBRARIES+= libhardware libcamera_client

LOCAL_MODULE := camera.aries
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

endif
