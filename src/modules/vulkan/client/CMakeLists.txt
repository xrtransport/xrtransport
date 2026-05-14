# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)

find_package(Vulkan REQUIRED)

if(XRTRANSPORT_DYNAMIC_MODULES)

    add_library(module_client_vulkan SHARED
        vulkan_client_module.cpp
        vulkan_core.cpp
        session_state.cpp
    )

    install(TARGETS module_client_vulkan
        LIBRARY DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
        RUNTIME DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
    )

    if(ANDROID)
        target_link_options(module_client_vulkan PRIVATE
            "-Wl,-rpath,$ORIGIN"
        )
    elseif(UNIX)
        set_target_properties(module_client_vulkan PROPERTIES
            INSTALL_RPATH "$ORIGIN"
        )
    endif()

else()

    add_library(module_client_vulkan STATIC
        vulkan_client_module.cpp
        vulkan_core.cpp
        session_state.cpp
    )

endif()

target_include_directories(module_client_vulkan PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/OpenXR-SDK/include
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
)

target_include_directories(module_client_vulkan PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(module_client_vulkan PRIVATE
    module_common_vulkan
    module_client_handle_exchange
    xrtransport_transport
    xrtransport_serialization
    Vulkan::Vulkan
)