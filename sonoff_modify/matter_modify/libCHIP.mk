#!/bin/bash
# This file is used to generate CHIP core library
#

# device type id: 0x010d
#MATTER_EXAMPLE = lighting-app
# device type id: 0x0013
#MATTER_EXAMPLE = bridge-app
# device type id: 0x010a
#MATTER_EXAMPLE = on-off-plug
# device controller
#MATTER_EXAMPLE = controller

CROSS_COMPILE = $(TOOLCHAIN_PATH)/$(TOOLCHAIN_PREFIX)

CHIP_DIR = $(BASEDIR)/../components/matter/connectedhomeip
OPENTHREAD_DIR = $(BASEDIR)/../components/openthread
ZAP_TOOL_PATH = $(CHIP_DIR)/zap
ifeq (,$(findstring ZAP_TOOL_PATH,$(PATH)))
	export PATH := $(ZAP_TOOL_PATH):$(PATH)
endif

# Compilation tools
AR = $(CROSS_COMPILE)ar
CXX = $(CROSS_COMPILE)g++
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
NM = $(CROSS_COMPILE)nm
LD = $(CROSS_COMPILE)gcc
GDB = $(CROSS_COMPILE)gdb
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

Q := @
ifeq ($(V),1)
Q := 
endif
OS := $(shell uname)

ifeq ($(ECHO),)
ECHO=echo
endif


# -------------------------------------------------------------------
# Include folder list
# -------------------------------------------------------------------
INCLUDES =
INCLUDES += -I$(BASEDIR)/middleware/arch/$(TARGET)/soc
INCLUDES += -I$(BASEDIR)/middleware/boards/$(TARGET)/partitions
# sonoff modify start
INCLUDES += -I$(PROJECT_BUILD_DIR)/partitions
INCLUDES += -I$(PROJECT_BUILD_DIR)/sys_persist_config
# sonoff modify end

ifneq ($(findstring $(TARGET),bk7258 bk7236 bk7236n bk7239n),)
INCLUDES += -I$(BASEDIR)/components/os_source/freertos_v10/include
INCLUDES += -I$(BASEDIR)/components/os_source/freertos_v10/portable/GCC/ARM_CM33_NTZ/non_secure
INCLUDES += -I$(BASEDIR)/middleware/arch/cm33/include
INCLUDES += -I$(BASEDIR)/middleware/soc/common/hal/include
INCLUDES += -I$(BASEDIR)/middleware/soc/$(TARGET)/hal
INCLUDES += -I$(BASEDIR)/middleware/soc/common/soc/include
INCLUDES += -I$(BASEDIR)/middleware/soc/$(TARGET)/soc
INCLUDES += -I$(BASEDIR)/middleware/soc/$(TARGET)
endif
ifneq ($(findstring $(TARGET),bk7235 bk7256),)
INCLUDES += -I$(BASEDIR)/components/os_source/freertos_v10/include
INCLUDES += -I$(BASEDIR)/components/os_source/freertos_v10/portable/GCC/RISC-V
INCLUDES += -I$(BASEDIR)/middleware/arch/riscv/include
INCLUDES += -I$(BASEDIR)/middleware/arch/riscv/include/bk_private
INCLUDES += -I$(CHIP_DIR)/src/platform/Beken/$(TARGET)
INCLUDES += -I$(BASEDIR)/middleware/driver/rtc
INCLUDES += -I$(BASEDIR)/middleware/driver/$(TARGET)
INCLUDES += -I$(BASEDIR)/middleware/soc/common/hal/include
INCLUDES += -I$(BASEDIR)/middleware/soc/$(TARGET)/hal
INCLUDES += -I$(BASEDIR)/middleware/soc/common/soc/include
INCLUDES += -I$(BASEDIR)/middleware/soc/$(TARGET)/soc

endif

INCLUDES += -I$(BASEDIR)/include
INCLUDES += -I$(BASEDIR)/include/soc/${TARGET}
INCLUDES += -I$(BASEDIR)/include/modules
INCLUDES += -I$(BASEDIR)/include/arch/compiler
INCLUDES += -I$(BASEDIR)/components/bk_ps/include/bk_private
INCLUDES += -I$(BASEDIR)/components/bk_common/include
INCLUDES += -I$(BASEDIR)/components/bk_common/include/bk_private
INCLUDES += -I$(BASEDIR)/components/bk_common/include/bk_private/legacy
INCLUDES += -I$(BASEDIR)/components/bk_config/include/cmake
INCLUDES += -I$(BASEDIR)/components/bk_log/include
INCLUDES += -I$(BASEDIR)/components/bk_rtos/include
INCLUDES += -I$(BASEDIR)/components/bk_rtos/include/bk_private
INCLUDES += -I$(BASEDIR)/components/bk_rtos/freertos
INCLUDES += -I$(BASEDIR)/components/bk_system/include
INCLUDES += -I$(BASEDIR)/components/bk_system
INCLUDES += -I$(BASEDIR)/components/bk_cli/include

INCLUDES += -I$(BASEDIR)/components/at/include
INCLUDES += -I$(BASEDIR)/components/bk_bluetooth/include
INCLUDES += -I$(BASEDIR)/components/bk_bluetooth/include/private

INCLUDES += -I$(BASEDIR)/components/bk_wifi/include
INCLUDES += -I$(BASEDIR)/components/bk_wifi/include/bk_private
INCLUDES += -I$(BASEDIR)/components/bk_wifi/include/bk_private/legacy
INCLUDES += -I$(BASEDIR)/components/bk_netif/include
INCLUDES += -I$(BASEDIR)/components/bk_netif/include/bk_private


INCLUDES += -I$(BASEDIR)/middleware/driver/
INCLUDES += -I$(BASEDIR)/middleware/driver/include
INCLUDES += -I$(BASEDIR)/middleware/driver/include/bk_private
INCLUDES += -I$(BASEDIR)/middleware/driver/include/bk_private/legacy
INCLUDES += -I$(BASEDIR)/middleware/driver/common
INCLUDES += -I$(BASEDIR)/middleware/driver/pwm
INCLUDES += -I$(BASEDIR)/middleware/driver/rc_beken
INCLUDES += -I$(BASEDIR)/middleware/driver/flash
INCLUDES += -I$(BASEDIR)/middleware/driver/rw_pub
INCLUDES += -I$(BASEDIR)/middleware/driver/uart
INCLUDES += -I$(BASEDIR)/middleware/driver/sys_ctrl
INCLUDES += -I$(BASEDIR)/middleware/driver/gpio
INCLUDES += -I$(BASEDIR)/middleware/driver/general_dma
INCLUDES += -I$(BASEDIR)/middleware/driver/icu
INCLUDES += -I$(BASEDIR)/middleware/driver/i2c
INCLUDES += -I$(BASEDIR)/middleware/driver/sdcard
INCLUDES += -I$(BASEDIR)/middleware/driver/saradc
INCLUDES += -I$(BASEDIR)/middleware/driver/pmu
INCLUDES += -I$(BASEDIR)/middleware/driver/mailbox
INCLUDES += -I$(BASEDIR)/middleware/driver/touch
INCLUDES += -I$(BASEDIR)/middleware/driver/sbc
INCLUDES += -I$(BASEDIR)/middleware/driver/rtc
INCLUDES += -I$(BASEDIR)/middleware/arch/common/soc/include
INCLUDES += -I$(BASEDIR)/middleware/arch/common/hal/include

INCLUDES += -I$(BASEDIR)/components/user_driver/include/bk_private
#INCLUDES += -I$(BASEDIR)/middleware/compal/matter
INCLUDES += -I$(BASEDIR)/../components/adapter/matter

INCLUDES += -I$(BASEDIR)/components/bluetooth/include
INCLUDES += -I$(BASEDIR)/components/lwip_intf_v2_1/lwip-2.1.2/port
INCLUDES += -I$(BASEDIR)/components/lwip_intf_v2_1/lwip-2.1.2/src/include
#INCLUDES += -I$(BASEDIR)/components/lwip_intf_v2_0/lwip-2.0.2/port
#INCLUDES += -I$(BASEDIR)/components/lwip_intf_v2_0/lwip-2.0.2/src/include

INCLUDES += -I$(BASEDIR)/include/components
INCLUDES += -I$(BASEDIR)/components/psa_mbedtls/mbedtls/include
INCLUDES += -I$(BASEDIR)/components/psa_mbedtls/mbedtls_port/inc
INCLUDES += -I$(BASEDIR)/components/psa_mbedtls/mbedtls_port/accelerator
INCLUDES += -I$(BASEDIR)/components/psa_mbedtls/mbedtls_port/accelerator/dubhe_alt/inc
INCLUDES += -I$(BASEDIR)/components/psa_mbedtls/mbedtls_port/accelerator/dubhe_driver/inc/crypto

INCLUDES += -I$(CONFIG_DIR)

# sonoff modify start
INCLUDES += $(addprefix -I,$(shell find $(SONOFF_ROOT)/sonoff -type d))
INCLUDES += -I$(PROJECT_DIR)/components/main/inc
# sonoff modify end

ifeq ($(EXTERNAL_PLATFORM), y)
INCLUDES += -I$(EXTERNAL_PLATFORM_DIR)/platform/Beken
else
INCLUDES += -I$(CHIP_DIR)/src/platform/Beken
endif
INCLUDES += -I$(OPENTHREAD_DIR)/openthread/include
INCLUDES += -I$(PARTITION_DIR)

# -------------------------------------------------------------------
# CHIP compile options
# -------------------------------------------------------------------
CFLAGS =

CFLAGS += -DCHIP_PROJECT=1
CFLAGS += -DCHIP_HAVE_CONFIG_H=1

CFLAGS += -DCFG_MBEDTLS=1
CFLAGS += -DMBEDTLS_CONFIG_FILE=\"$(BASEDIR)/components/psa_mbedtls/mbedtls_port/configs/mbedtls_psa_crypto_config.h\"
#CFLAGS += -DLWIP_IPV6=1

#CFLAGS += -DLWIP_IPV6_ND=1
#CFLAGS += -DLWIP_IPV6_SCOPES=1
#CFLAGS += -DLWIP_PBUF_FROM_CUSTOM_POOLS=0
#CFLAGS += -DLWIP_IPV6_ROUTE_TABLE_SUPPORT=1

CFLAGS += -DCHIP_DEVICE_LAYER_NONE=0
CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_ZEPHYR_NET_IF=0
CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_BSD_IFADDRS=0
CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_ZEPHYR_SOCKET_EXTENSIONS=0

#CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_LWIP=1
CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_SOCKETS=0
CFLAGS += -DCHIP_SYSTEM_CONFIG_USE_NETWORK_FRAMEWORK=0
CFLAGS += -DCHIP_ADDRESS_RESOLVE_IMPL_INCLUDE_HEADER="<lib/address_resolve/AddressResolve_DefaultImpl.h>"

ifeq ($(DYNAMIC_LOAD_MATTER), on)
CFLAGS += -fPIE
CFLAGS += -msingle-pic-base
CFLAGS += -mpic-register=r9
CFLAGS += -fomit-frame-pointer
CFLAGS += -mno-pic-data-is-text-relative
CFLAGS += -mthumb
# CFLAGS += -mlong-calls
endif

CXXFLAGS =
ifneq ($(findstring $(TARGET),bk7236n bk7239n),)
CFLAGS += -mcpu=cortex-m33+nodsp -mfloat-abi=soft -mcmse
CXXFLAGS += --specs=nosys.specs --specs=nano.specs
CXXFLAGS += -nostdlib
ifeq ($(DYNAMIC_LOAD_MATTER), on)
CXXFLAGS += -fPIE
CXXFLAGS += -msingle-pic-base
CXXFLAGS += -mpic-register=r9
CXXFLAGS += -fomit-frame-pointer
CXXFLAGS += -mno-pic-data-is-text-relative
CXXFLAGS += -mthumb
CXXFLAGS += -fno-use-cxa-atexit
# CXXFLAGS += -mlong-calls
endif
endif
ifneq ($(findstring $(TARGET),bk7235 bk7256),)
CXXFLAGS += -mstrict-align
CXXFLAGS += -Wl,--defsym,memcpy=memcpy_ss
endif

CXXFLAGS += -Wno-conversion
CXXFLAGS += -Os
CXXFLAGS += -Wno-error
CXXFLAGS += -Wno-sign-compare
CXXFLAGS += -Wno-unused-function
CXXFLAGS += -Wno-unused-but-set-variable
CXXFLAGS += -Wno-unused-variable
#CXXFLAGS += -Wno-deprecated-declarations
CXXFLAGS += -Wno-unused-parameter
#CXXFLAGS += -Wno-format
CXXFLAGS += -Wno-literal-suffix
CXXFLAGS += -std=gnu++17
CXXFLAGS += -fno-rtti
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-builtin-printf
CXXFLAGS += -fno-builtin-sprintf
CXXFLAGS += -fno-builtin-snprintf


CHIP_CFLAGS = $(CFLAGS)
CHIP_CFLAGS += $(INCLUDES)

CHIP_CXXFLAGS += $(CFLAGS)
CHIP_CXXFLAGS += $(CXXFLAGS)
CHIP_CXXFLAGS += $(INCLUDES)


export CHIP_ROOT_ENV=$(CHIP_DIR)
export BUILD_ROOT_ENV=$(CHIP_DIR)/build
export ZAP_INSTALL_PATH=$(CHIP_DIR)/zap

#*****************************************************************************#
#                        RULES TO GENERATE libCHIP.a and libAPPLICATION.a     #
#*****************************************************************************#

# Define the Rules to build the core targets
all: CHIP_CORE

CHIP_CORE: 
	@echo "target=$(TARGET)"
	@echo "toolchain=$(TOOLCHAIN_PATH) prefix=$(TOOLCHAIN_PREFIX)"
	@echo "base_dir=$(BASEDIR)"
	@echo "config=$(CONFIG_DIR)"
	@echo "output_dir=$(OUTPUT_DIR)"
	@if [ ! -d $(OUTPUT_DIR) ]; then \
		mkdir -p $(OUTPUT_DIR); mkdir -p $(OUTPUT_DIR)/app_obj;\
	fi
	@if [ ! -d $(OUTPUT_DIR)/app_obj ]; then \
		mkdir -p $(OUTPUT_DIR)/app_obj;\
	fi
	@echo "Compiling CHIP SDK static library"
	@echo                                   > $(OUTPUT_DIR)/args.gn
	@if [ "$(EXTERNAL_PLATFORM)" = "y" ]; then \
		cat    args.gn.in          >> $(OUTPUT_DIR)/args.gn; \
		echo "Cat argns.gn.in to output_dir args.gn"; \
	else \
		echo "import(\"//args.gni\")"          >> $(OUTPUT_DIR)/args.gn; \
		echo "import args.gni"; \
	fi
	@echo target_cflags_c  = [$(foreach word,$(CHIP_CFLAGS),\"$(word)\",)] | sed -e 's/=\"/=\\"/g;s/\"\"/\\"\"/g;'  >> $(OUTPUT_DIR)/args.gn
	@echo target_cflags_cc = [$(foreach word,$(CHIP_CXXFLAGS),\"$(word)\",)] | sed -e 's/=\"/=\\"/g;s/\"\"/\\"\"/g;'   >> $(OUTPUT_DIR)/args.gn
	@echo beken_ar = \"$(AR)\"    >> $(OUTPUT_DIR)/args.gn
	@echo beken_cc = \"$(CC)\"   >> $(OUTPUT_DIR)/args.gn
	@echo beken_cxx = \"$(CXX)\"  >> $(OUTPUT_DIR)/args.gn
	@# sonoff modify start
	@if [ -f "$(PROJECT_DIR)/matter/BUILD.gn" ]; then \
		echo "Using product Matter app: $(PROJECT_DIR)/matter"; \
		cd $(PROJECT_DIR)/matter && gn gen --check --fail-on-unused-args $(OUTPUT_DIR) && ninja -C $(OUTPUT_DIR);\
	elif [ -n "${MATTER_EXAMPLE}" ]; then \
		cd $(CHIP_DIR)/examples/${MATTER_EXAMPLE}/beken && gn gen --check --fail-on-unused-args $(OUTPUT_DIR);\
		cd $(CHIP_DIR)/examples/${MATTER_EXAMPLE}/beken ; ninja -C $(OUTPUT_DIR);\
	else \
		cd $(CHIP_DIR)/config/beken/ && gn gen --check --fail-on-unused-args $(OUTPUT_DIR);\
		cd $(CHIP_DIR)/config/beken/ ; ninja -C $(OUTPUT_DIR);\
	fi
	@# sonoff modify end

.PHONY: clean
clean:
	rm -rf $(OUTPUT_DIR)/
