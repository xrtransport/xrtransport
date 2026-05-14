# SPDX-License-Identifier: LGPL-3.0-or-later

add_library(xrtransport_config STATIC
    config.cpp
)

# Set target properties
target_include_directories(xrtransport_config PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/json/include
)

# config needs to be hard-coded in some way, because we don't control how the client runtime is launched
# on Linux, we store it in a JSON file,
# on Android, we store it in system properties,
# on Windows, we store it in the Windows registry (TODO)
if(UNIX AND NOT ANDROID)
    target_compile_definitions(xrtransport_config PRIVATE XRTRANSPORT_CONFIG_PATH=\"/var/xrtransport/config.json\")
endif()