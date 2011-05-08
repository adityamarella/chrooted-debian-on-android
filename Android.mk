LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := bootdeb
LOCAL_SRC_FILES := bootdeb.c
LOCAL_CFLAGS += -DHAVE_ANDROID

LOCAL_LDLIBS += -llog


include $(BUILD_EXECUTABLE)
