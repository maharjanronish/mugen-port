LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := game
LOCAL_C_INCLUDES := $(LOCAL_PATH)/engine $(LOCAL_PATH)/SDL/include
LOCAL_CFLAGS := -O2 -w
LOCAL_SRC_FILES := \
    engine/util.c \
    engine/sff.c \
    engine/air.c \
    engine/expr.c \
    engine/char.c \
    engine/state.c \
    engine/fight.c \
    engine/stage.c \
    engine/game.c \
    engine/platform_sdl.c
LOCAL_SHARED_LIBRARIES := SDL2
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -llog -landroid
include $(BUILD_SHARED_LIBRARY)

include $(LOCAL_PATH)/SDL/Android.mk
