LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/LFL_BIN_DIR.mk
include $(CLEAR_VARS)
LOCAL_MODULE := app
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../$(LFL_BIN_DIR)/term/libapp.so
include $(PREBUILT_SHARED_LIBRARY)

#include $(call all-subdir-makefiles)
