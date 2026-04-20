SHELL := /bin/bash

APP_NAME := central-scrutinizer
PAK_DIR_NAME := Central Scrutinizer.pak
RELEASE_NAME := Central Scrutinizer
BUILD_DIR := build
DIST_DIR := $(BUILD_DIR)/release
STAGING_DIR := $(BUILD_DIR)/staging
APOSTROPHE_DIR := third_party/apostrophe
APOSTROPHE_BRANCH := main
TG5040_TOOLCHAIN := ghcr.io/loveretro/tg5040-toolchain:latest
TG5050_TOOLCHAIN := ghcr.io/loveretro/tg5050-toolchain:latest
MY355_TOOLCHAIN := ghcr.io/loveretro/my355-toolchain:latest
ADB ?= adb
SDL_AVAILABLE := $(shell pkg-config --exists sdl2 SDL2_ttf SDL2_image 2>/dev/null && echo 1 || echo 0)
ifeq ($(SDL_AVAILABLE),1)
	SDL_CFLAGS := $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
	SDL_LDFLAGS := $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)
	MAC_UI_CFLAGS := -DCS_ENABLE_APOSTROPHE_UI $(SDL_CFLAGS)
	MAC_UI_LDFLAGS := $(SDL_LDFLAGS)
else
	MAC_UI_CFLAGS :=
	MAC_UI_LDFLAGS :=
endif
SRC_COMMON := src/build_info.c src/paths.c src/auth.c src/session.c src/platforms.c src/states.c src/dotclean.c src/library.c src/uploads.c src/file_ops.c src/settings.c src/keep_awake.c src/ui.c
SRC_SERVER := src/daemon.c src/terminal.c src/app.c src/server.c src/routes_status.c src/routes_auth.c src/routes_helpers.c src/routes_library.c src/routes_states.c src/routes_logs.c src/routes_upload.c src/routes_file_ops.c src/routes_tools.c third_party/civetweb/src/civetweb.c
SRC_VENDOR := third_party/qrcodegen.c
SRC_APP := src/main.c $(SRC_COMMON) $(SRC_SERVER) $(SRC_VENDOR)
COMMON_INCLUDES := -Iinclude -Ithird_party/civetweb/include -I$(APOSTROPHE_DIR)/include -DUSE_WEBSOCKET
WEB_DEPS_STAMP := web/node_modules/next/package.json

.PHONY: all mac tg5040 tg5050 my355 package package-local package-tg5040 package-tg5050 package-my355 do-package deploy deploy-platform clean test-native test-native-all test-smoke test-all web-install web-test web-build preview preview-clear-port update-apostrophe

$(APOSTROPHE_DIR)/include/apostrophe.h:
	git submodule update --init --checkout $(APOSTROPHE_DIR)

update-apostrophe: $(APOSTROPHE_DIR)/include/apostrophe.h
	@set -euo pipefail; \
	if [ -n "$$(git -C "$(APOSTROPHE_DIR)" status --porcelain)" ]; then \
		echo "Error: $(APOSTROPHE_DIR) has local modifications. Commit or discard them before updating." >&2; \
		git -C "$(APOSTROPHE_DIR)" status --short >&2; \
		exit 1; \
	fi; \
	git submodule sync -- "$(APOSTROPHE_DIR)"; \
	git -C "$(APOSTROPHE_DIR)" fetch origin "$(APOSTROPHE_BRANCH):refs/remotes/origin/$(APOSTROPHE_BRANCH)"; \
	git submodule update --init --remote --checkout "$(APOSTROPHE_DIR)"; \
	commit=$$(git -C "$(APOSTROPHE_DIR)" rev-parse HEAD); \
	echo "Apostrophe pinned to $$commit"

mac:
	@mkdir -p $(BUILD_DIR)/mac
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DNO_SSL $(COMMON_INCLUDES) $(MAC_UI_CFLAGS) \
		-o $(BUILD_DIR)/mac/$(APP_NAME) $(SRC_APP) $(MAC_UI_LDFLAGS) -lm -lpthread

all: tg5040 tg5050 my355

tg5040:
	@mkdir -p $(BUILD_DIR)/tg5040
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(TG5040_TOOLCHAIN) \
		make -C /workspace -f ports/tg5040/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/tg5040

tg5050:
	@mkdir -p $(BUILD_DIR)/tg5050
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(TG5050_TOOLCHAIN) \
		make -C /workspace -f ports/tg5050/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/tg5050

my355:
	@mkdir -p $(BUILD_DIR)/my355
	docker run --rm \
		-v "$(CURDIR)":/workspace \
		$(MY355_TOOLCHAIN) \
		make -C /workspace -f ports/my355/Makefile BUILD_DIR=/workspace/$(BUILD_DIR)/my355

test-native:
	@if [ -z "$(strip $(TEST))" ]; then \
		echo "ERROR: make test-native requires TEST=tests/native/test_build_info.c" >&2; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/tests
	cc -std=gnu11 -O0 -g -DPLATFORM_MAC -DNO_SSL $(if $(TEST_SERVER),-DCS_TESTING,) $(COMMON_INCLUDES) \
		-o $(BUILD_DIR)/tests/$(notdir $(TEST:.c=)) \
		$(TEST) $(SRC_COMMON) $(if $(TEST_SERVER),$(SRC_SERVER) $(SRC_VENDOR),) -lm -lpthread
	@./$(BUILD_DIR)/tests/$(notdir $(TEST:.c=))
	@echo "PASS $(TEST)"

test-native-all:
	@set -e; \
	for test_file in tests/native/test_*.c; do \
		echo "RUN native $$test_file"; \
		$(MAKE) test-native TEST=$$test_file TEST_SERVER=1; \
	done

test-smoke:
	@for script in tests/smoke/test_*.sh; do \
		[ "$$(basename "$$script")" = "helpers.sh" ] || bash "$$script"; \
	done

test-all:
	@$(MAKE) test-native-all
	@$(MAKE) web-test
	@$(MAKE) test-smoke

$(WEB_DEPS_STAMP): web/package.json web/package-lock.json
	npm --prefix web ci

web-install: $(WEB_DEPS_STAMP)

web-test: $(WEB_DEPS_STAMP)
	npm --prefix web test -- --run
	npm --prefix web run test:e2e

web-build: $(WEB_DEPS_STAMP)
	npm --prefix web run build

package-tg5040: tg5040 web-build
	@$(MAKE) do-package PLATFORM=tg5040 BIN_SRC=$(BUILD_DIR)/tg5040/$(APP_NAME)

package-tg5050: tg5050 web-build
	@$(MAKE) do-package PLATFORM=tg5050 BIN_SRC=$(BUILD_DIR)/tg5050/$(APP_NAME)

package-my355: my355 web-build
	@$(MAKE) do-package PLATFORM=my355 BIN_SRC=$(BUILD_DIR)/my355/$(APP_NAME)

do-package:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(BIN_SRC)" ]; then \
		echo "Error: do-package requires PLATFORM and BIN_SRC."; \
		exit 1; \
	fi
	@rm -rf "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)"
	@mkdir -p "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)/resources/web"
	@cp "$(BIN_SRC)" "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)/$(APP_NAME)"
	@cp launch.sh pak.json "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)/"
	@cp -a web/out/. "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)/resources/web/"

package: package-tg5040 package-tg5050 package-my355
	@rm -rf $(STAGING_DIR)
	@mkdir -p $(DIST_DIR)/all
	@for platform in tg5040 tg5050 my355; do \
		mkdir -p "$(STAGING_DIR)/Tools/$$platform"; \
		cp -a "$(BUILD_DIR)/$$platform/$(PAK_DIR_NAME)" "$(STAGING_DIR)/Tools/$$platform/"; \
	done
	@rm -f "$(DIST_DIR)/all/$(RELEASE_NAME).pakz"
	@cd "$(STAGING_DIR)" && zip -9 -r "$(CURDIR)/$(DIST_DIR)/all/$(RELEASE_NAME).pakz" . -x '.*'

package-local: mac web-build
	@rm -rf $(STAGING_DIR)
	@mkdir -p $(DIST_DIR)/local
	@for platform in tg5040 tg5050 my355; do \
		pak_dir="$(STAGING_DIR)/Tools/$$platform/$(PAK_DIR_NAME)"; \
		mkdir -p "$$pak_dir/resources/web"; \
		cp "$(BUILD_DIR)/mac/$(APP_NAME)" "$$pak_dir/$(APP_NAME)"; \
		cp launch.sh pak.json "$$pak_dir/"; \
		cp -a web/out/. "$$pak_dir/resources/web/"; \
	done
	@rm -f "$(DIST_DIR)/local/$(RELEASE_NAME)-local.pakz"
	@cd "$(STAGING_DIR)" && zip -9 -r "$(CURDIR)/$(DIST_DIR)/local/$(RELEASE_NAME)-local.pakz" . -x '.*'

deploy:
	@echo "Detecting platform..."
	@SERIAL="$(ADB_SERIAL)"; \
	if [ -z "$$SERIAL" ]; then \
		SERIAL=$$($(ADB) devices | awk 'NR>1 && $$2=="device" {print $$1; exit}'); \
	fi; \
	if [ -z "$$SERIAL" ]; then \
		echo "Error: No online adb device found."; \
		exit 1; \
	fi; \
	ADB_CMD="$(ADB) -s $$SERIAL"; \
	FINGERPRINT=$$($$ADB_CMD shell ' \
		cat /proc/device-tree/compatible 2>/dev/null; \
		echo; \
		cat /proc/device-tree/model 2>/dev/null; \
		echo; \
		uname -a 2>/dev/null' 2>/dev/null | tr '\000' '\n' | tr -d '\r'); \
	case "$$FINGERPRINT" in \
		*rk3566*|*miyoo-355*) PLATFORM=my355 ;; \
		*allwinner,a523*|*sun55iw3*) PLATFORM=tg5050 ;; \
		*allwinner,a133*|*sun50iw*) PLATFORM=tg5040 ;; \
		*allwinner*) \
			if printf '%s' "$$FINGERPRINT" | grep -qi 'a523'; then \
				PLATFORM=tg5050; \
			else \
				PLATFORM=tg5040; \
			fi \
			;; \
		*) \
			echo "Error: Could not detect a supported platform from adb fingerprint."; \
			echo "  Serial: $$SERIAL"; \
			echo "  Fingerprint snippet: $$(printf '%s' "$$FINGERPRINT" | head -c 240)"; \
			exit 1; \
			;; \
	esac; \
	echo "Detected adb serial: $$SERIAL"; \
	echo "Detected platform: $$PLATFORM"; \
	$(MAKE) deploy-platform PLATFORM=$$PLATFORM SERIAL=$$SERIAL

deploy-platform:
	@if [ -z "$(PLATFORM)" ] || [ -z "$(SERIAL)" ]; then \
		echo "Error: deploy-platform requires PLATFORM and SERIAL."; \
		exit 1; \
	fi
	@$(MAKE) package-$(PLATFORM)
	@ADB_CMD="$(ADB) -s $(SERIAL)"; \
	PAK_ROOT="/mnt/SDCARD/Tools/$(PLATFORM)"; \
	PAK_DIR="$$PAK_ROOT/$(PAK_DIR_NAME)"; \
	echo "Deploying $(PAK_DIR_NAME) to $$PAK_DIR..."; \
	$$ADB_CMD shell "rm -rf '$$PAK_DIR' && mkdir -p '$$PAK_ROOT'"; \
	$$ADB_CMD push "$(BUILD_DIR)/$(PLATFORM)/$(PAK_DIR_NAME)" "$$PAK_ROOT/"; \
	echo "Deploy complete."

PREVIEW_PORT ?= 8877
# Dev-only fixed preview PIN. Production pairing codes rotate on startup.
PREVIEW_PIN ?= 7391
PREVIEW_SDCARD ?= fixtures/mock_sdcard

preview-clear-port:
	@PIDS="$$(lsof -tiTCP:$(PREVIEW_PORT) -sTCP:LISTEN 2>/dev/null | sort -u)"; \
	if [ -n "$$PIDS" ]; then \
		echo "Stopping existing listener(s) on preview port $(PREVIEW_PORT)..."; \
		for PID in $$PIDS; do \
			CMD=$$(ps -p "$$PID" -o command= 2>/dev/null); \
			if [ -n "$$CMD" ]; then \
				echo "  $$PID $$CMD"; \
			else \
				echo "  $$PID"; \
			fi; \
		done; \
		kill $$PIDS; \
		CLEARED=0; \
		for _ in $$(seq 1 50); do \
			if ! lsof -tiTCP:$(PREVIEW_PORT) -sTCP:LISTEN >/dev/null 2>&1; then \
				CLEARED=1; \
				break; \
			fi; \
			sleep 0.1; \
		done; \
		if [ "$$CLEARED" -ne 1 ]; then \
			echo "Error: preview port $(PREVIEW_PORT) is still in use after terminating existing listener(s)." >&2; \
			lsof -nP -iTCP:$(PREVIEW_PORT) -sTCP:LISTEN >&2 || true; \
			exit 1; \
		fi; \
	fi

preview: mac web-build
	@$(MAKE) --no-print-directory preview-clear-port PREVIEW_PORT=$(PREVIEW_PORT)
	@echo "Pairing code: $(PREVIEW_PIN)"
	@echo "Open http://127.0.0.1:$(PREVIEW_PORT)"
	CS_PAIRING_CODE=$(PREVIEW_PIN) ./$(BUILD_DIR)/mac/$(APP_NAME) \
		--headless --port $(PREVIEW_PORT) --web-root web/out --sdcard $(PREVIEW_SDCARD)

clean:
	rm -rf $(BUILD_DIR)
