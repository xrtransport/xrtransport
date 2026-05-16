# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

add_subdirectory(common/serialization)
add_subdirectory(common/transport)

add_subdirectory(modules)

# Conditional inclusion based on build flags
if(XRTRANSPORT_BUILD_CLIENT)
    add_subdirectory(client)
    add_subdirectory(common/config)
endif()

if(XRTRANSPORT_BUILD_SERVER)
    add_subdirectory(server)
endif()
