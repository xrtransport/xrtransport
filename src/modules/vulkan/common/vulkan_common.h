// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_VULKAN2_COMMON_H
#define XRTRANSPORT_VULKAN2_COMMON_H

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

#define XRTP_MSG_VULKAN2_GET_PHYSICAL_DEVICE 100
#define XRTP_MSG_VULKAN2_RETURN_PHYSICAL_DEVICE 101
#define XRTP_MSG_VULKAN2_CREATE_SWAPCHAIN 102
#define XRTP_MSG_VULKAN2_CREATE_SWAPCHAIN_RETURN 103
#define XRTP_MSG_VULKAN2_DESTROY_SWAPCHAIN 104
#define XRTP_MSG_VULKAN2_DESTROY_SWAPCHAIN_RETURN 105
#define XRTP_MSG_VULKAN2_CREATE_SESSION 106
#define XRTP_MSG_VULKAN2_CREATE_SESSION_RETURN 107
#define XRTP_MSG_VULKAN2_DESTROY_SESSION 108
#define XRTP_MSG_VULKAN2_DESTROY_SESSION_RETURN 109
#define XRTP_MSG_VULKAN2_RELEASE_SWAPCHAIN_IMAGE 110
#define XRTP_MSG_VULKAN2_RELEASE_SWAPCHAIN_IMAGE_RETURN 111
#define XRTP_MSG_VULKAN2_GET_SWAPCHAIN_IMAGE_HANDLES 112
#define XRTP_MSG_VULKAN2_RETURN_SWAPCHAIN_IMAGE_HANDLES 113

// this needs to be exactly in common between client and server for export/import
static inline VkImageCreateInfo create_vk_image_create_info(const XrSwapchainCreateInfo& create_info) {
    VkImageCreateInfo image_create_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_create_info.imageType = VK_IMAGE_TYPE_2D;
    image_create_info.format = static_cast<VkFormat>(create_info.format);
    image_create_info.extent = {create_info.width, create_info.height, 1};
    image_create_info.mipLevels = create_info.mipCount;
    image_create_info.arrayLayers = create_info.arraySize * create_info.faceCount;
    image_create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    // works as long as sampleCount is a power of 2 up to 64
    image_create_info.samples = static_cast<VkSampleCountFlagBits>(create_info.sampleCount);
    image_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT)
        image_create_info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        image_create_info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT); // TODO: ignoring for now
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT)
        image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT)
        image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_SAMPLED_BIT)
        image_create_info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT)
        image_create_info.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_INPUT_ATTACHMENT_BIT_KHR)
        image_create_info.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (create_info.faceCount == 6)
        image_create_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    
    return image_create_info;
}

static inline VkImageAspectFlags get_aspect_from_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return VK_IMAGE_ASPECT_DEPTH_BIT;

        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

#endif // XRTRANSPORT_VULKAN2_COMMON_H