# Build from this directory:
#   make MODEL=onoff_plug
#   make MODEL=onoff_plug SOC=bk7239n
#   make MODEL=onoff_plug SOC=bk7239n menuconfig
#   make MODEL=onoff_plug clean
#   make restore

SHELL := /bin/bash

BUILD_TOOL_DIR  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ROOT            := $(abspath $(BUILD_TOOL_DIR)/..)
SDK_DIR         := $(ROOT)/bk_openthread
IDK_DIR         := $(SDK_DIR)/bk_idk
OVERLAY_ROOT    := $(ROOT)/sonoff_modify
IDK_OVERLAY     := $(OVERLAY_ROOT)/idk_modify
MATTER_OVERLAY  := $(OVERLAY_ROOT)/matter_modify
OPENTHREAD_OVERLAY := $(OVERLAY_ROOT)/openthread_modify
ADAPTER_OVERLAY := $(OVERLAY_ROOT)/adapter_modify
BUILD_DIR       := $(ROOT)/build
CHIP_DIR        := $(SDK_DIR)/components/matter/connectedhomeip
OPENTHREAD_DIR  := $(SDK_DIR)/components/openthread

COMMON_CMAKE    := $(BUILD_TOOL_DIR)/CMakeLists.txt
COMMON_CONFIG   := $(BUILD_TOOL_DIR)/config
COMMON_MATTER   := $(BUILD_TOOL_DIR)/matter
PRODUCT_CMAKE   := $(BUILD_TOOL_DIR)/product/CMakeLists.txt

SOC             ?= bk7239n

ifeq ($(filter help restore clean,$(MAKECMDGOALS)),)
ifeq ($(MODEL),)
$(error please specify MODEL, e.g. make MODEL=onoff_plug)
endif
endif

SONOFF_DIR       := $(ROOT)/sonoff
PROJECT_SRC     := $(ROOT)/project/$(MODEL)
PROJECT_DIR     := $(BUILD_DIR)/project_tree/$(MODEL)

.PHONY: all build menuconfig clean restore help apply_overlay

all: build

help:
	@echo ""
	@echo "  make MODEL=<name>              build project (default SOC=$(SOC))"
	@echo "  make MODEL=<name> SOC=bk7239n  build for a specific SoC"
	@echo "  make MODEL=<name> menuconfig   configure project (default SOC=$(SOC))"
	@echo "  make MODEL=<name> clean        remove that project's build output"
	@echo "  make restore                   restore bk_openthread / bk_idk via git"
	@echo ""
	@echo "  overlay: sonoff_modify/idk_modify       -> bk_openthread/bk_idk"
	@echo "           sonoff_modify/matter_modify    -> bk_openthread/components/matter"
	@echo "           sonoff_modify/openthread_modify -> bk_openthread/components/openthread"
	@echo "           sonoff_modify/adapter_modify   -> bk_openthread/components/adapter"
	@echo ""
	@echo "  shared : config/  matter/  product/CMakeLists.txt -> components/main"
	@echo "  product: ../project/\$$(MODEL)/src  ../project/\$$(MODEL)/inc  + ../sonoff/"
	@echo ""

define overlay_copy
	src_root="$(1)"; dst_root="$(2)"; \
	if [ -d "$$src_root" ]; then \
		find "$$src_root" -type f ! -name '.gitkeep' -print0 | while IFS= read -r -d '' f; do \
			rel="$${f#$$src_root/}"; \
			dst="$$dst_root/$$rel"; \
			mkdir -p "$$(dirname "$$dst")"; \
			cp -f "$$f" "$$dst"; \
			echo "[sonoff] overlay $$rel"; \
		done; \
	fi
endef

define restore_sdk
	git -C "$(SDK_DIR)" restore --source=HEAD --staged --worktree -- .; \
	if git -C "$(IDK_DIR)" rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
		git -C "$(IDK_DIR)" restore --source=HEAD --staged --worktree -- .; \
	fi; \
	find "$(SDK_DIR)/components/matter/connectedhomeip" \
		-type d -name '__pycache__' -prune -exec rm -rf {} + 2>/dev/null || true; \
	echo "[sonoff] restored SDK"
endef

# sonoff/ and project/<MODEL>/src+inc -> components/main (Armino auto-links main).
define assemble_project
	mkdir -p "$(PROJECT_DIR)/matter/third_party" "$(PROJECT_DIR)/components/main"; \
	ln -sfn "$(COMMON_CMAKE)" "$(PROJECT_DIR)/CMakeLists.txt"; \
	ln -sfn "$(COMMON_CONFIG)" "$(PROJECT_DIR)/config"; \
	ln -sfn "$(PRODUCT_CMAKE)" "$(PROJECT_DIR)/components/main/CMakeLists.txt"; \
	ln -sfn "$(SONOFF_DIR)" "$(PROJECT_DIR)/components/main/sonoff"; \
	if [ -d "$(PROJECT_SRC)/src" ]; then \
		ln -sfn "$(PROJECT_SRC)/src" "$(PROJECT_DIR)/components/main/src"; \
	fi; \
	if [ -d "$(PROJECT_SRC)/inc" ]; then \
		ln -sfn "$(PROJECT_SRC)/inc" "$(PROJECT_DIR)/components/main/inc"; \
	fi; \
	echo "[sonoff] main component: sonoff + $(PROJECT_SRC)/src"; \
	ln -sfn "$(COMMON_MATTER)/BUILD.gn" "$(PROJECT_DIR)/matter/BUILD.gn"; \
	ln -sfn "$(COMMON_MATTER)/args.gni" "$(PROJECT_DIR)/matter/args.gni"; \
	ln -sfn "$(COMMON_MATTER)/.gn" "$(PROJECT_DIR)/matter/.gn"; \
	ln -sfn "$(COMMON_MATTER)/list_src.py" "$(PROJECT_DIR)/matter/list_src.py"; \
	ln -sfn "$(PROJECT_SRC)/matter/src" "$(PROJECT_DIR)/matter/src"; \
	ln -sfn "$(PROJECT_SRC)/matter/inc" "$(PROJECT_DIR)/matter/inc"; \
	ln -sfn "$(PROJECT_SRC)/matter/zap" "$(PROJECT_DIR)/matter/zap"; \
	ln -sfn "$(CHIP_DIR)/examples/build_overrides" "$(PROJECT_DIR)/matter/build_overrides"; \
	ln -sfn "$(CHIP_DIR)" "$(PROJECT_DIR)/matter/third_party/connectedhomeip"; \
	echo "[sonoff] assembled $(PROJECT_DIR)"
endef

define check_layout
	if [ ! -d "$(PROJECT_SRC)" ]; then \
		echo "project not found: $(PROJECT_SRC)"; \
		exit 1; \
	fi; \
	if [ ! -f "$(COMMON_CMAKE)" ]; then \
		echo "missing $(COMMON_CMAKE)"; \
		exit 1; \
	fi; \
	if [ ! -d "$(COMMON_CONFIG)" ]; then \
		echo "missing $(COMMON_CONFIG)"; \
		exit 1; \
	fi; \
	if [ ! -f "$(COMMON_MATTER)/BUILD.gn" ]; then \
		echo "missing $(COMMON_MATTER)/BUILD.gn"; \
		exit 1; \
	fi
endef

apply_overlay:
	@$(call check_layout)
	@$(call overlay_copy,$(IDK_OVERLAY),$(IDK_DIR))
	@$(call overlay_copy,$(MATTER_OVERLAY),$(SDK_DIR)/components/matter)
	@$(call overlay_copy,$(OPENTHREAD_OVERLAY),$(OPENTHREAD_DIR))
	@$(call overlay_copy,$(ADAPTER_OVERLAY),$(SDK_DIR)/components/adapter)

restore:
	@$(call restore_sdk)

build:
	@set -e; \
	restore() { $(call restore_sdk); }; \
	trap restore EXIT; \
	$(call check_layout); \
	$(call assemble_project); \
	$(call overlay_copy,$(IDK_OVERLAY),$(IDK_DIR)); \
	$(call overlay_copy,$(MATTER_OVERLAY),$(SDK_DIR)/components/matter); \
	$(call overlay_copy,$(OPENTHREAD_OVERLAY),$(OPENTHREAD_DIR)); \
	$(call overlay_copy,$(ADAPTER_OVERLAY),$(SDK_DIR)/components/adapter); \
	echo "[sonoff] MODEL=$(MODEL) SOC=$(SOC)"; \
	echo "[sonoff] PROJECT_DIR=$(PROJECT_DIR)"; \
	export SONOFF_ROOT="$(ROOT)"; \
	export MODEL="$(MODEL)"; \
	$(MAKE) -C "$(SDK_DIR)" $(SOC) \
		PROJECT="$(MODEL)" \
		PROJECT_DIR="$(PROJECT_DIR)" \
		BUILD_DIR="$(BUILD_DIR)"

menuconfig:
	@set -e; \
	restore() { $(call restore_sdk); }; \
	trap restore EXIT; \
	$(call check_layout); \
	$(call assemble_project); \
	$(call overlay_copy,$(IDK_OVERLAY),$(IDK_DIR)); \
	$(call overlay_copy,$(MATTER_OVERLAY),$(SDK_DIR)/components/matter); \
	$(call overlay_copy,$(OPENTHREAD_OVERLAY),$(OPENTHREAD_DIR)); \
	$(call overlay_copy,$(ADAPTER_OVERLAY),$(SDK_DIR)/components/adapter); \
	echo "[sonoff] menuconfig MODEL=$(MODEL) SOC=$(SOC)"; \
	export SONOFF_ROOT="$(ROOT)"; \
	export MODEL="$(MODEL)"; \
	$(MAKE) -C "$(IDK_DIR)" menuconfig \
		ARMINO_SOC="$(SOC)" \
		ARMINO_SOC_NAME="$(SOC)" \
		PROJECT="$(MODEL)" \
		PROJECT_DIR="$(PROJECT_DIR)" \
		BUILD_DIR="$(BUILD_DIR)"

clean:
ifeq ($(MODEL),)
	@echo "rm -rf $(BUILD_DIR)"
	@rm -rf "$(BUILD_DIR)"
else
	@echo "rm -rf $(BUILD_DIR)/$(SOC)/$(MODEL) $(BUILD_DIR)/project_tree/$(MODEL)"
	@rm -rf "$(BUILD_DIR)/$(SOC)/$(MODEL)" "$(BUILD_DIR)/project_tree/$(MODEL)"
endif
