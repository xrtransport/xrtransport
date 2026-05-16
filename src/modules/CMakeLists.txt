# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)

if(NOT XRTRANSPORT_DYNAMIC_MODULES)
    if(XRTRANSPORT_BUILD_CLIENT)
        add_library(static_client_modules STATIC
            static_client_modules.cpp
        )
        target_include_directories(static_client_modules PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
    if(XRTRANSPORT_BUILD_SERVER)
        add_library(static_server_modules STATIC
            static_server_modules.cpp
        )
        target_include_directories(static_server_modules PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
    endif()
endif()

option(XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE "Build Handle Exchange Module" ON)
option(XRTRANSPORT_BUILD_MODULE_VULKAN "Build Vulkan Module" ON)

if(XRTRANSPORT_BUILD_FOR_GFXSTREAM)
    # handle exchange is not needed for gfxstream build because it uses a custom mechanism to share
    # already-imported handles with the client
    set(XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE OFF)
endif()

if(XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE)
    if(WIN32)
        set(HANDLE_EXCHANGE_DIR "handle_exchange_win32")
    elseif(UNIX)
        set(HANDLE_EXCHANGE_DIR "handle_exchange_linux")
    endif()
    add_subdirectory(${HANDLE_EXCHANGE_DIR})

    if(NOT XRTRANSPORT_DYNAMIC_MODULES)
        if(XRTRANSPORT_BUILD_CLIENT)
            target_compile_definitions(static_client_modules PRIVATE XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE)
            target_link_libraries(static_client_modules PUBLIC module_client_handle_exchange)
        endif()

        if(XRTRANSPORT_BUILD_SERVER)
            target_compile_definitions(static_server_modules PRIVATE XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE)
            target_link_libraries(static_server_modules PUBLIC module_server_handle_exchange)
        endif()
    endif()
endif()
if(XRTRANSPORT_BUILD_MODULE_VULKAN)
    add_subdirectory(vulkan)

    if(NOT XRTRANSPORT_DYNAMIC_MODULES)
        if(XRTRANSPORT_BUILD_CLIENT)
            target_compile_definitions(static_client_modules PRIVATE XRTRANSPORT_BUILD_MODULE_VULKAN)
            target_link_libraries(static_client_modules PUBLIC module_client_vulkan)
        endif()

        if(XRTRANSPORT_BUILD_SERVER)
            target_compile_definitions(static_server_modules PRIVATE XRTRANSPORT_BUILD_MODULE_VULKAN)
            target_link_libraries(static_server_modules PUBLIC module_server_vulkan)
        endif()
    endif()
endif()

