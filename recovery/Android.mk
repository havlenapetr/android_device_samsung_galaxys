ifneq ($(filter galaxys,$(TARGET_DEVICE)),)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_SRC_FILES := recovery_ui.c

# should match TARGET_RECOVERY_UI_LIB set in BoardConfig.mk
LOCAL_MODULE := librecovery_ui_aries

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += bootable/recovery/minui
LOCAL_SRC_FILES := graphics.c

# should match TARGET_RECOVERY_GRAPHICS_LIB set in BoardConfig.mk
LOCAL_MODULE := librecovery_graphics_aries

include $(BUILD_STATIC_LIBRARY)

endif
