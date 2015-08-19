LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := term
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../android/term/liblterm.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := lfapp
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../android/term/lterm_lfapp_obj/liblterm_lfapp.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := Box2D
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../android/core/imports/Box2D/Box2D/libBox2D.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libpng
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../android/core/imports/libpng/libpng.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libjpeg-turbo
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../android/core/imports/libjpeg-turbo/.libs/libturbojpeg.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libcrypto
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../core/imports/OpenSSL-for-Android-Prebuilt/openssl-1.0.2/armeabi-v7a/lib/libcrypto.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libssl
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../core/imports/OpenSSL-for-Android-Prebuilt/openssl-1.0.2/armeabi-v7a/lib/libssl.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := lfjni
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../../core/lfapp/jni/lfjni.cpp
LOCAL_LDLIBS := -lGLESv2 -lGLESv1_CM -llog -lz
LOCAL_STATIC_LIBRARIES := term lfapp Box2D libpng libjpeg-turbo libssl libcrypto
include $(BUILD_SHARED_LIBRARY)

#include $(call all-subdir-makefiles)
