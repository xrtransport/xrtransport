// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_VULKAN2_CLIENT_SESSION_STATE_H
#define XRTRANSPORT_VULKAN2_CLIENT_SESSION_STATE_H

#include "image_type.h"
#include "vulkan_loader.h"

#include "xrtransport/transport/transport.h"

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <unordered_set>
#include <optional>
#include <queue>
#include <future>

class SwapchainState;
class SessionState;
class SwapchainImage;

std::optional<std::reference_wrapper<SwapchainState>> get_swapchain_state(XrSwapchain handle);
std::optional<std::reference_wrapper<SessionState>> get_session_state(XrSession handle);

SwapchainState& store_swapchain_state(
    XrSwapchain handle,
    XrSession parent_handle,
    std::vector<SwapchainImage>&& images,
    uint32_t width,
    uint32_t height,
    bool is_static,
    ImageType image_type,
    xrtransport::VulkanLoader& vk
);
SessionState& store_session_state(
    XrSession handle,
    const XrGraphicsBindingVulkan2KHR& graphics_binding,
    VkQueue queue,
    VkCommandPool command_pool
);

void destroy_swapchain_state(XrSwapchain handle);
void destroy_session_state(XrSession handle);

struct SwapchainImage {
    XrSwapchainImageVulkan2KHR image;
    VkDeviceMemory memory;
    VkSemaphore rendering_done;
    VkSemaphore copying_done;
    VkFence copying_done_fence;
    VkCommandBuffer acquire_command_buffer;
    VkCommandBuffer release_command_buffer;
    bool has_been_acquired = false;

    VkImageAspectFlags aspect;
    uint32_t num_levels;
    uint32_t num_layers;
};

class SwapchainState {
private:
    // using unique_ptr because SwapchainImage is not moveable
    std::vector<SwapchainImage> images;

    // This mutex guards all of the mutable state
    std::mutex mutex;
    
    // keeps track of how many images have been acquired
    uint32_t num_acquired = 0;

    // ring buffer heads indicating which images have been acquired, waited, and released
    uint32_t acquire_head = 0;
    uint32_t wait_head = 0;
    uint32_t release_head = 0;

    // if true, size is 1 and the image can only be acquired once
    bool is_static;

    xrtransport::VulkanLoader& vk;

    // retrieved from the parent SessionState and stored here for convenience
    VkDevice device;
    VkQueue queue;

public:
    XrSwapchain handle;
    XrSession parent_handle;
    uint32_t width;
    uint32_t height;
    ImageType image_type;

    explicit SwapchainState(
        XrSwapchain handle,
        XrSession parent_handle,
        std::vector<SwapchainImage>&& images,
        uint32_t width,
        uint32_t height,
        bool is_static,
        ImageType image_type,
        xrtransport::VulkanLoader& vk
    );

    XrResult acquire(uint32_t& index_out);
    XrResult wait(XrDuration timeout);
    XrResult release(uint32_t& index_out);

    const std::vector<SwapchainImage>& get_images() const {
        return images;
    }
};

struct SessionState {
    XrSession handle;
    XrGraphicsBindingVulkan2KHR graphics_binding;
    VkQueue queue;
    VkCommandPool command_pool;
    std::unordered_set<XrSwapchain> swapchains;

    explicit SessionState(
        XrSession handle,
        const XrGraphicsBindingVulkan2KHR& graphics_binding,
        VkQueue queue,
        VkCommandPool command_pool
    );
};

#endif // XRTRANSPORT_VULKAN2_CLIENT_SESSION_STATE_H