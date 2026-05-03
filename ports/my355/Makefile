SHELL := /bin/bash

CROSS_COMPILE ?= aarch64-nextui-linux-gnu-
CC := $(CROSS_COMPILE)gcc
STRIP := $(CROSS_COMPILE)strip
SRC := $(shell find /workspace/src -name '*.c' -print | sort) /workspace/third_party/qrcodegen.c /workspace/third_party/civetweb/src/civetweb.c
HDR := $(shell find /workspace/include -name '*.h' -print | sort) /workspace/third_party/civetweb/include/civetweb.h
INCLUDES := -I/workspace/include -I/workspace/third_party/civetweb/include -I/workspace/third_party/apostrophe/include
PKG_CONFIG := pkg-config
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LDFLAGS := $(shell $(PKG_CONFIG) --libs sdl2) -lSDL2_ttf -lSDL2_image

CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	-mcpu=cortex-a55 -mtune=cortex-a55 \
	-DPLATFORM_MY355 -DNO_SSL -DCS_ENABLE_APOSTROPHE_UI -DUSE_WEBSOCKET -DHAVE_POLL \
	-ffunction-sections -fdata-sections \
	$(INCLUDES) $(SDL_CFLAGS)

LDFLAGS := $(SDL_LDFLAGS) -Wl,--gc-sections -lpthread -lutil -lm

all: $(BUILD_DIR)/central-scrutinizer

$(BUILD_DIR)/central-scrutinizer: $(SRC) $(HDR)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	$(STRIP) $@
