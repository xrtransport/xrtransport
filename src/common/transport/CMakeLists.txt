# SPDX-License-Identifier: LGPL-3.0-or-later

# Create static library for xrtransport transport
if(XRTRANSPORT_DYNAMIC_MODULES)
    add_library(xrtransport_transport SHARED
        transport_impl.cpp
        transport_c_api.cpp
    )
else()
    add_library(xrtransport_transport STATIC
        transport_impl.cpp
        transport_c_api.cpp
    )
endif()

# Set target properties for transport
target_include_directories(xrtransport_transport PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/OpenXR-SDK/include
    ${CMAKE_SOURCE_DIR}/external/asio/asio/include
)

target_compile_definitions(xrtransport_transport PRIVATE XRTRANSPORT_EXPORT_API)