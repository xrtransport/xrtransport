# SPDX-License-Identifier: LGPL-3.0-or-later

# Create static library for xrtransport serialization
add_library(xrtransport_serialization STATIC
    serializer.cpp
    deserializer.cpp
    custom_serializer.cpp
    custom_deserializer.cpp
    struct_size.cpp
)

# Set target properties
target_include_directories(xrtransport_serialization PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/external/OpenXR-SDK/include
    ${PROJECT_SOURCE_DIR}/external/asio/asio/include
)

# Set C++ standard
set_target_properties(xrtransport_serialization PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Export the target for use by other parts of the project
target_compile_features(xrtransport_serialization PUBLIC cxx_std_17)