LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

#TARGET_IPC_LIB := libsamsung-ipc_aries
TARGET_IPC_LIB_CFLAGS := -DDEVICE_IPC_V4 -DIPC_DEVICE_EXPLICIT=\"aries\"

LOCAL_MODULE := libsamsung-ipc_aries
LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS := $(TARGET_IPC_LIB_CFLAGS)

LOCAL_C_INCLUDES := \
    hardware/ril/samsung-ril/samsung-ipc \
    external/openssl/include

LOCAL_SRC_FILES := \
    aries_ipc.c

LOCAL_SHARED_LIBRARIES := libutils libcrypto

include $(BUILD_STATIC_LIBRARY)
