# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.14)

# Client-side OpenXR transport implementation
message(STATUS "Building xrtransport client components")

# Client library
add_library(xrtransport_client SHARED
    rpc.cpp
    runtime.cpp
    entry.cpp
    available_extensions.cpp
    synchronization.cpp
    function_table.cpp
    module_loader.cpp
)

if(XRTRANSPORT_DYNAMIC_MODULES)
    target_compile_definitions(xrtransport_client PRIVATE XRTRANSPORT_DYNAMIC_MODULES)
else()
    target_link_libraries(xrtransport_client PRIVATE static_client_modules)
endif()

# Dependencies
target_link_libraries(xrtransport_client PRIVATE
    xrtransport_transport
    xrtransport_serialization
    xrtransport_config
)

# Include directories
target_include_directories(xrtransport_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/external/asio/asio/include
)

if(XRTRANSPORT_BUILD_FOR_GFXSTREAM)
    add_subdirectory(gfxstream_asg)
    target_link_libraries(xrtransport_client PRIVATE gfxstream_asg)
    target_compile_definitions(xrtransport_client PRIVATE XRTRANSPORT_BUILD_FOR_GFXSTREAM)
endif()

if(XRTRANSPORT_CREATE_MANIFEST)
    add_openxr_manifest(xrtransport_client_manifest.json $<TARGET_FILE:xrtransport_client>)
    install_openxr_manifest(xrtransport_client_manifest.json $<TARGET_FILE:xrtransport_client> ${XRTRANSPORT_CLIENT_INSTALL_PATH})
endif()

# setup install directory
install(TARGETS xrtransport_client
    LIBRARY DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
    RUNTIME DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH} # needed for Windows, for some reason it's not considered a LIBRARY target on Windows
)
install(TARGETS xrtransport_transport
    LIBRARY DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
    RUNTIME DESTINATION ${XRTRANSPORT_CLIENT_INSTALL_PATH}
)

if(ANDROID)
    target_link_options(xrtransport_client PRIVATE
        "-Wl,-rpath,$ORIGIN"
    )
elseif(UNIX)
    set_target_properties(xrtransport_client PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif()