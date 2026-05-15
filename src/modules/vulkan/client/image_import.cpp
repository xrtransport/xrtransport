#include "image_import.h"

#include "vulkan_common.h"
#include "xrtransport/transport/transport.h"
#include "xrtransport/serialization/serializer.h"
#include "xrtransport/serialization/deserializer.h"

// only import handle exchange related headers if we're not building for gfxstream
#ifndef XRTRANSPORT_BUILD_FOR_GFXSTREAM
#include "image_handles.h"
#endif

#include <stdexcept>

using namespace xrtransport;

// This file contains implementations for importing images from the server using two different approaches:
// 
// - Send the VkDevice and XrSwapchain handles directly to the host and get valid VkImage, VkDeviceMemory,
//   and VkSemaphore handles back
//   (gfxstream build configuration)
// 
// - Read FDs from the handle exchange plugin and import the images and semaphores.
//   (default build configuration for Waydroid or native)

#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
std::vector<ImportedImage> import_images(
    VkDevice vk_device,
    xrtransport::Transport& transport,
    XrSwapchain swapchain_handle,
    uint32_t num_images,
    VkImageCreateInfo vk_create_info,
    uint64_t memory_size,
    uint32_t memory_type_index
) {
    auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_GET_SWAPCHAIN_IMAGE_HANDLES);
    SerializeContext s_ctx(msg_out.buffer);
    serialize(&vk_device, s_ctx);
    serialize(&swapchain_handle, s_ctx);
    msg_out.flush();

    std::vector<ImportedImage> result(num_images);

    auto msg_in = transport.await_message(XRTP_MSG_VULKAN2_RETURN_SWAPCHAIN_IMAGE_HANDLES);
    DeserializeContext d_ctx(msg_in.buffer);

    uint32_t ok{};
    deserialize(&ok, d_ctx);

    if (!ok) {
        throw std::runtime_error("Server does not support sending Vulkan handles directly");
    }

    for (size_t i = 0; i < num_images; i++) {
        deserialize(&result[i].image, d_ctx);
        deserialize(&result[i].memory, d_ctx);
        deserialize(&result[i].rendering_done, d_ctx);
        deserialize(&result[i].copying_done, d_ctx);
    }

    return result;
}

#else // XRTRANSPORT_BUILD_FOR_GFXSTREAM

static VkSemaphore import_semaphore(const VulkanLoader& vk, VkDevice device, xrtp_Handle handle) {
    VkResult result{};

    VkSemaphoreCreateInfo create_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

    VkSemaphore semaphore{};
    result = vk.CreateSemaphore(device, &create_info, nullptr, &semaphore);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create semaphore: " + std::to_string(result));
    }

#ifdef _WIN32
    #error TODO
#else
    VkImportSemaphoreFdInfoKHR import_info{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    import_info.semaphore = semaphore;
    import_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    import_info.fd = static_cast<int>(handle);

    result = vk.ImportSemaphoreFdKHR(device, &import_info);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to import semaphore: " + std::to_string(result));
    }
#endif

    return semaphore;
}

std::vector<ImportedImage> import_images(
    xrtransport::Transport& transport,
    const VulkanLoader& vk,
    VkDevice device,
    XrSwapchain swapchain_handle,
    std::uint32_t num_images,
    VkImageCreateInfo vk_create_info,
    std::uint64_t memory_size,
    std::uint32_t memory_type_index
) {
    VkResult vk_result{};

    std::vector<ImportedImage> result;
    result.reserve(num_images);

    for (uint32_t i = 0; i < num_images; i++) {
        ImageHandles image_handles = read_image_handles();

        VkExternalMemoryImageCreateInfo external_create_info{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
#ifdef _WIN32
        external_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        external_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
        
        vk_create_info.pNext = &external_create_info;
        vk_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        VkImageAspectFlags aspect = get_aspect_from_format(vk_create_info.format);
        uint32_t num_levels = vk_create_info.mipLevels;
        uint32_t num_layers = vk_create_info.arrayLayers;

        VkImage image{};
        vk_result = vk.CreateImage(device, &vk_create_info, nullptr, &image);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create VkImage: " + std::to_string(vk_result));
        }

#ifdef _WIN32
        #error TODO
#else
        VkImportMemoryFdInfoKHR import_info{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
        import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        import_info.fd = image_handles.memory_handle;
#endif

        VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.pNext = &import_info;
        alloc_info.allocationSize = memory_size;
        alloc_info.memoryTypeIndex = memory_type_index;

        VkDeviceMemory image_memory{};
        vk_result = vk.AllocateMemory(device, &alloc_info, nullptr, &image_memory);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to import memory from FD: " + std::to_string(vk_result));
        }

        vk_result = vk.BindImageMemory(device, image, image_memory, 0);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind memory to image: " + std::to_string(vk_result));
        }

#ifdef _WIN32
        #error Close handle if needed
#else
        // No need to close the FD, the driver closes it upon successful import.
#endif

        VkSemaphore rendering_done = import_semaphore(vk, device, image_handles.rendering_done_handle);
        VkSemaphore copying_done = import_semaphore(vk, device, image_handles.copying_done_handle);

        result.emplace_back(ImportedImage{
            .image = image,
            .memory = image_memory,
            .rendering_done = rendering_done,
            .copying_done = copying_done
        });
    }

    return result;
}

#endif // XRTRANSPORT_BUILD_FOR_GFXSTREAM