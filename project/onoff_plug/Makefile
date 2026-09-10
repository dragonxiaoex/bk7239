# Primary entry is: cd build_tool && make MODEL=onoff_plug
MODEL ?= $(notdir $(CURDIR))
SOC   ?= bk7239n
BUILD_TOOL := $(abspath ../../build_tool)

.PHONY: all clean restore

all:
	$(MAKE) -C "$(BUILD_TOOL)" MODEL="$(MODEL)" SOC="$(SOC)"

clean:
	$(MAKE) -C "$(BUILD_TOOL)" MODEL="$(MODEL)" SOC="$(SOC)" clean

restore:
	$(MAKE) -C "$(BUILD_TOOL)" restore
