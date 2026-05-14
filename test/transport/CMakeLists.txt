# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)

# Find and link threading support
find_package(Threads REQUIRED)

# Create transport server executable
add_executable(transport_server
    transport_server.cpp
)

target_include_directories(transport_server PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/asio/asio/include
)

target_link_libraries(transport_server PRIVATE
    xrtransport_transport
    Threads::Threads
)

if(MSVC)
    target_compile_options(transport_server PRIVATE /Zc:preprocessor)
endif()

# Create transport unit tests executable
add_executable(transport_tests
    transport_tests.cpp
    shared_buffer.cpp
    test_duplex_stream.cpp
)

target_include_directories(transport_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/asio/asio/include
)

target_link_libraries(transport_tests PRIVATE
    Catch2::Catch2WithMain
    xrtransport_transport
    Threads::Threads
)

if(MSVC)
    target_compile_options(transport_tests PRIVATE /Zc:preprocessor)
endif()

# Create transport integration tests executable
add_executable(transport_integration_tests
    transport_integration_tests.cpp
)

target_include_directories(transport_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/asio/asio/include
)

target_link_libraries(transport_integration_tests PRIVATE
    Catch2::Catch2
    xrtransport_transport
    Threads::Threads
)

# Add dependency so integration tests can launch the server
add_dependencies(transport_integration_tests transport_server)

if(MSVC)
    target_compile_options(transport_integration_tests PRIVATE /Zc:preprocessor)
endif()