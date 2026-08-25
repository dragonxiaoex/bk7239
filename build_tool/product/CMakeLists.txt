# Unified app component (registered as Armino `main`):
# sonoff/**/*.c + project/<MODEL>/src/*.c
# Every directory under sonoff/ is an include path, so
#   #include "sonoff_wifi.h"
# works without the wifi/ prefix.
#
# Do not use CONFIGURE_DEPENDS: Armino evaluates this file in cmake
# script mode (component_get_requirements), which rejects that flag.
set(incs
    .
    inc
    sonoff
)

file(GLOB_RECURSE sonoff_src "${CMAKE_CURRENT_LIST_DIR}/sonoff/*.c")
file(GLOB_RECURSE sonoff_inc "${CMAKE_CURRENT_LIST_DIR}/sonoff/*.h")
file(GLOB product_src "${CMAKE_CURRENT_LIST_DIR}/src/*.c")

foreach(f ${sonoff_src} ${sonoff_inc})
    get_filename_component(dir "${f}" DIRECTORY)
    file(RELATIVE_PATH rel "${CMAKE_CURRENT_LIST_DIR}" "${dir}")
    list(APPEND incs ${rel})
endforeach()
list(REMOVE_DUPLICATES incs)

set(srcs ${sonoff_src} ${product_src})

if (CONFIG_OVERRIDE_FLASH_PARTITION)
    list(APPEND srcs vendor_flash.c)
endif()

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}")
