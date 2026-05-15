// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_VULKAN2_CLIENT_IMAGE_IMPORT_H
#define XRTRANSPORT_VULKAN2_CLIENT_IMAGE_IMPORT_H

#include "xrtransport/transport/transport.h"
#include "vulkan_loader.h"

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

struct ImportedImage {
    VkImage image;
    VkDeviceMemory memory;
    VkSemaphore rendering_done;
    VkSemaphore copying_done;
};

std::vector<ImportedImage> import_images(
    xrtransport::Transport& transport,
    const VulkanLoader& vk,
    VkDevice device,
    XrSwapchain swapchain_handle,
    std::uint32_t num_images,
    VkImageCreateInfo vk_create_info,
    std::uint64_t memory_size,
    std::uint32_t memory_type_index);

#endif // XRTRANSPORT_VULKAN2_CLIENT_IMAGE_IMPORT_H
