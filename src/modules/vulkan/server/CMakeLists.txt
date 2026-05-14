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
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/OpenXR-SDK/include
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
)

target_include_directories(module_server_vulkan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(module_server_vulkan PRIVATE
    module_common_vulkan
    xrtransport_transport
    xrtransport_serialization
    Vulkan::Vulkan
)

if(XRTRANSPORT_BUILD_FOR_GFXSTREAM)
    target_compile_definitions(module_server_vulkan PRIVATE
        XRTRANSPORT_BUILD_FOR_GFXSTREAM
    )
else()
    target_link_libraries(module_server_vulkan PRIVATE
        module_server_handle_exchange
    )
endif()