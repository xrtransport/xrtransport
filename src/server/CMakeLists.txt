# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.10)

# Server-side OpenXR transport implementation
message(STATUS "Building xrtransport server components")

# Server library
add_library(xrtransport_server STATIC
    server.cpp
    function_dispatch.cpp
    module_loader.cpp
)

if(XRTRANSPORT_DYNAMIC_MODULES)
    target_compile_definitions(xrtransport_server PRIVATE XRTRANSPORT_DYNAMIC_MODULES)
else()
    target_link_libraries(xrtransport_server PUBLIC static_server_modules)
endif()

# Dependencies
target_link_libraries(xrtransport_server PUBLIC
    xrtransport_transport
    xrtransport_serialization
    openxr_loader
)

# Include directories
target_include_directories(xrtransport_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/asio/asio/include
    ${CMAKE_SOURCE_DIR}/external/OpenXR-SDK/include
)

# Server executable
add_executable(xrtransport_server_main
    main.cpp
)

# Dependencies (include directories inherited)
target_link_libraries(xrtransport_server_main
    xrtransport_server
)

# setup install directory
install(TARGETS xrtransport_server_main RUNTIME DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH})
install(TARGETS xrtransport_transport
    LIBRARY DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
    RUNTIME DESTINATION ${XRTRANSPORT_SERVER_INSTALL_PATH}
)

if(UNIX)
    set_target_properties(xrtransport_server_main PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif()
