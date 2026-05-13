# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)

find_package(Vulkan REQUIRED)

if (XRTRANSPORT_DYNAMIC_MODULES)
    add_library(module_server_vulkan SHARED
        vulkan_server_module.cpp
    )
    
    install(TARGETS module_server_vulkan
        LIBRARY DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
        RUNTIME DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
    )

    if(UNIX)
        set_target_properties(module_server_vulkan PROPERTIES
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
else()
    add_library(module_server_vulkan STATIC
        vulkan_server_module.cpp
    )
endif()

target_include_directories(module_server_vulkan PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/OpenXR-SDK/include
    ${CMAKE_SOURCE_DIR}/external/spdlog/include
)

target_include_directories(module_server_vulkan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(module_server_vulkan PRIVATE
    module_common_vulkan
    module_server_handle_exchange
    xrtransport_transport
    xrtransport_serialization
    Vulkan::Vulkan
)

