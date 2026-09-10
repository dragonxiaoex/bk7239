set(incs .)
set(srcs)

if (CONFIG_DYNAMIC_LOAD_MATTER)
list(APPEND srcs
	matter_mod.c
	)
endif()

armino_build_get_property(target ARMINO_SOC)


armino_component_register(SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES bk_common adapter openthread part_table
	)
	
if (CONFIG_SUPPORT_MATTER)
#######################################################
set(chip_external_platform_path "")
if (CONFIG_EXTERNAL_PLATFORM)
set(chip_gn_args "import(\"//args.gni\")\n")

macro(chip_gn_arg_append arg val)
    string(APPEND chip_gn_args "${arg} = ${val}\n")
endmacro()

macro(chip_gn_arg_bool arg boolean)
    if (${boolean})
        string(APPEND chip_gn_args "${arg} = true\n")
    else()
        string(APPEND chip_gn_args "${arg} = false\n")
    endif()
endmacro()

set(chip_external_platform_path "$ENV{ARMINO_PATH}/${CONFIG_EXTERNAL_PLATFORM_DIR}")
chip_gn_arg_append("chip_device_platform" "\"external\"")
chip_gn_arg_append("chip_external_platform_include_dir"  "\"${chip_external_platform_path}\"")
chip_gn_arg_append("chip_platform_target"  "\"${chip_external_platform_path}/platform/Beken\"")

chip_gn_arg_append("chip_mdns"                    "\"minimal\"")
#chip_gn_arg_bool("chip_mdns_minimal"              "true")
chip_gn_arg_bool("chip_enable_additional_data_advertising"              "false")
#chip_gn_arg_bool("chip_mdns_platform"             "false")
chip_gn_arg_bool("chip_enable_wifi"               "true")


set(args_gn_input "${COMPONENT_DIR}/args.gn.in")
file(GENERATE OUTPUT "${args_gn_input}" CONTENT "${chip_gn_args}")
endif()
#######################################################

set(chip_dir ${COMPONENT_DIR})
set(base_dir $ENV{ARMINO_PATH})
set(config_dir ${CMAKE_BINARY_DIR}/config)
set(output_dir ${CMAKE_BINARY_DIR}/armino/matter_build)
set(lib_dir ${output_dir}/lib)
set(toolchain ${CONFIG_TOOLCHAIN_PATH})
set(toolchain_prefix "arm-none-eabi-")
set(partition_dir $ENV{PARTITIONS_DIR})
set(external_platform ${CONFIG_EXTERNAL_PLATFORM})

if (CONFIG_DYNAMIC_LOAD_MATTER)
set(dynamic_load_matter "on")
else()
set(dynamic_load_matter "off")
endif()

if (CONFIG_MATTER_EXAMPLE)
	set(matter_example ${CONFIG_MATTER_EXAMPLE})
	set(matter_lib "libMatterApp.a")
else()
	set(matter_example "")
	set(matter_lib "libCHIP.a")
endif()

include(ExternalProject)
ExternalProject_Add(matter_project
	SOURCE_DIR ${chip_dir}
	CONFIGURE_COMMAND ""
	BUILD_IN_SOURCE 1
	BUILD_COMMAND
		make TARGET=${target}
		MATTER_EXAMPLE=${matter_example}
		EXTERNAL_PLATFORM=${external_platform}
		EXTERNAL_PLATFORM_DIR=${chip_external_platform_path}
		DYNAMIC_LOAD_MATTER=${dynamic_load_matter}
		TOOLCHAIN_PATH=${toolchain}
		TOOLCHAIN_PREFIX=${toolchain_prefix}
		CONFIG_DIR=${config_dir}
		BASEDIR=${base_dir}
		OUTPUT_DIR=${output_dir}
		PARTITION_DIR=${partition_dir}
		# sonoff modify start
		PROJECT_DIR=$ENV{PROJECT_DIR}
		PROJECT_BUILD_DIR=$ENV{PROJECT_BUILD_DIR}
		SONOFF_ROOT=$ENV{SONOFF_ROOT}
		# sonoff modify end
		-f ${chip_dir}/libCHIP.mk
	BUILD_BYPRODUCTS ${lib_dir}/${matter_lib}
	BUILD_ALWAYS 1
	INSTALL_COMMAND ""
	)


add_library(lib_matter STATIC IMPORTED GLOBAL)
add_dependencies(lib_matter matter_project)
set_target_properties(lib_matter PROPERTIES IMPORTED_LOCATION ${lib_dir}/${matter_lib})

add_dependencies(${COMPONENT_LIB} lib_matter)

if (CONFIG_DYNAMIC_LOAD_MATTER)

set(mod_build_tool ${base_dir}/components/udynlink/scripts/mkmodule)
set(mod_build_opt --public-symbols ChipTest --no-prologue --gen-c-header --header-path ${COMPONENT_DIR} --disasm)
set(mod_build_cmd ${python} ${mod_build_tool} ${mod_build_opt} ${lib_dir}/${matter_lib})
MESSAGE(STATUS "${mod_build_cmd}")

execute_process(COMMAND ${mod_build_cmd}
				OUTPUT_VARIABLE output
				RESULT_VARIABLE result)
	MESSAGE(STATUS "${output}")
	if(result STREQUAL 0)
		MESSAGE(STATUS "mkmodule execute success...")
	else()
		MESSAGE(STATUS "mkmodule execute failed...")
	endif()

else()
target_link_libraries(${COMPONENT_LIB} INTERFACE lib_matter)
endif()

endif()
