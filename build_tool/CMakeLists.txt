# Shared Armino project file. Project name comes from MODEL.
cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED ENV{SONOFF_ROOT})
    message(FATAL_ERROR "SONOFF_ROOT is not set")
endif()
if(NOT DEFINED ENV{MODEL})
    message(FATAL_ERROR "MODEL is not set")
endif()

set(EXTRA_COMPONENTS_DIRS
    $ENV{ARMINO_PATH}/../components
)
include($ENV{ARMINO_PATH}/tools/build_tools/cmake/project.cmake)
project($ENV{MODEL})
