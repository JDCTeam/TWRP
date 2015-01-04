LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(commands_recovery_local_path)
LOCAL_SRC_FILES := simg2img.c sparse_crc32.c
LOCAL_SHARED_LIBRARIES += libz libc

LOCAL_MODULE := simg2img_twrp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_STEM := simg2img

include $(BUILD_EXECUTABLE)
