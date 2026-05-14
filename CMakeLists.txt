# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(xrtransport)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(XRTRANSPORT_BUILD_CLIENT "Build client" ON)
option(XRTRANSPORT_BUILD_SERVER "Build server" ON)
option(XRTRANSPORT_BUILD_TESTS "Build tests" ON)
option(XRTRANSPORT_CREATE_MANIFEST "Create OpenXR Manifest" ON)
set(XRTRANSPORT_CLIENT_INSTALL_PATH client CACHE STRING
    "Folder within the install prefix to put client libraries"
)
set(XRTRANSPORT_SERVER_INSTALL_PATH server CACHE STRING
    "Folder within the install prefix to put server libraries"
)
option(XRTRANSPORT_DYNAMIC_MODULES "Use dynamic linking for modules" OFF)
option(XRTRANSPORT_BUILD_FOR_GFXSTREAM "Build client and server for integration with gfxstream in the goldfish emulator" OFF)

# gfxstream and dynamic modules are mutually exclusive
if(XRTRANSPORT_DYNAMIC_MODULES AND XRTRANSPORT_BUILD_FOR_GFXSTREAM)
    message(FATAL_ERROR "Dynamic modules are not allowed for the gfxstream build")
endif()

if (XRTRANSPORT_DYNAMIC_MODULES)
    # global compile definition so that sources can build for dynamic vs static modules
    add_compile_definitions(XRTRANSPORT_DYNAMIC_MODULES)
endif()

if (XRTRANSPORT_BUILD_SERVER)
    # Server needs OpenXR loader to be built
    add_subdirectory(external/OpenXR-SDK EXCLUDE_FROM_ALL)
endif()

# Set C++ standard for the entire project
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# For spdlog
if(MSVC)
    add_compile_options(/utf-8)
    # Enable up to Windows 10 features
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

# Add include directories for the project
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/OpenXR-SDK/include
    ${PROJECT_SOURCE_DIR}/external/spdlog/include
    ${PROJECT_SOURCE_DIR}/external/asio/asio/include
)

# Build with PIC so that static libraries can be linked into shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

function(add_openxr_manifest filename library_path)
    if(WIN32)
        get_filename_component(target_name ${filename} NAME_WE)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${filename}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/${filename}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${filename}
                -DLIBRARY_PATH=${library_path}
                -P ${PROJECT_SOURCE_DIR}/create_openxr_manifest.cmake
            COMMAND_EXPAND_LISTS
        )
        add_custom_target(${target_name} ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/${filename}")
    else()
        get_filename_component(target_name ${filename} NAME_WE)
        add_custom_command(
            OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${filename}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/${filename}
                -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/${filename}
                -DLIBRARY_PATH=${library_path}
                -P ${PROJECT_SOURCE_DIR}/create_openxr_manifest.cmake
            COMMAND_EXPAND_LISTS
        )
        add_custom_target(${target_name} ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${filename}")
    endif()
    
endfunction()

function(install_openxr_manifest filename library_path install_path)
    if(WIN32)
        message(FATAL_ERROR "TODO: implement")
    else()
        install(CODE "
            get_filename_component(library_filename ${library_path} NAME)
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -DINPUT_PATH=${CMAKE_CURRENT_SOURCE_DIR}/${filename}
                    -DOUTPUT_PATH=${CMAKE_INSTALL_PREFIX}/${install_path}/${filename}
                    -DLIBRARY_PATH=./\${library_filename}
                    -P ${PROJECT_SOURCE_DIR}/create_openxr_manifest.cmake
            )
        ")
    endif()
    
endfunction()

# Build xrtransport
add_subdirectory(src)

# Build the test executables
if(XRTRANSPORT_BUILD_TESTS)
    add_subdirectory(test)
endif()