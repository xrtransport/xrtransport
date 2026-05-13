# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.15)

add_library(gfxstream_asg STATIC
    AddressSpaceStream.cpp
    goldfish_address_space.cpp
    GoldfishAddressSpaceStream.cpp
    ring_buffer.cpp
    VirtioGpuAddressSpaceStream.cpp
)

target_include_directories(gfxstream_asg PUBLIC include)