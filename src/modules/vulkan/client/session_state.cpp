// SPDX-License-Identifier: LGPL-3.0-or-later

#include "session_state.h"

#include "vulkan_common.h"

#include "xrtransport/transport/transport.h"
#include "xrtransport/serialization/serializer.h"

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

#include <spdlog/spdlog.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <optional>
#include <queue>
#include <future>
#include <unordered_map>
#include <tuple>

using namespace xrtransport;

namespace {

std::unordered_map<XrSwapchain, SwapchainState> swapchain_states;
std::unordered_map<XrSession, SessionState> session_states;

} // namespace

std::optional<std::reference_wrapper<SwapchainState>> get_swapchain_state(XrSwapchain handle) {
    auto it = swapchain_states.find(handle);
    if (it == swapchain_states.end()) {
        return std::nullopt;
    }
    else {
        return it->second;
    }
}

std::optional<std::reference_wrapper<SessionState>> get_session_state(XrSession handle) {
    auto it = session_states.find(handle);
    if (it == session_states.end()) {
        return std::nullopt;
    }
    else {
        return it->second;
    }
}

SwapchainState& store_swapchain_state(
    XrSwapchain handle,
    XrSession parent_handle,
    std::vector<SwapchainImage>&& images,
    uint32_t width,
    uint32_t height,
    bool is_static,
    ImageType image_type,
    VulkanLoader& vk
) {
    return swapchain_states.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(handle),
        std::forward_as_tuple(
            handle,
            parent_handle,
            std::move(images),
            width,
            height,
            is_static,
            image_type,
            vk
        )
    ).first->second;
}

SessionState& store_session_state(
    XrSession handle,
    const XrGraphicsBindingVulkan2KHR& graphics_binding,
    VkQueue queue,
    VkCommandPool command_pool
) {
    return session_states.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(handle),
        std::forward_as_tuple(
            handle,
            graphics_binding,
            queue,
            command_pool
        )
    ).first->second;
}

void destroy_swapchain_state(XrSwapchain handle) {
    swapchain_states.erase(handle);
}

void destroy_session_state(XrSession handle) {
    session_states.erase(handle);
}

SwapchainState::SwapchainState(
    XrSwapchain handle,
    XrSession parent_handle,
    std::vector<SwapchainImage>&& images,
    uint32_t width,
    uint32_t height,
    bool is_static,
    ImageType image_type,
    VulkanLoader& vk
)
    : handle(handle),
    parent_handle(parent_handle),
    images(std::move(images)),
    is_static(is_static),
    width(width),
    height(height),
    image_type(image_type),
    vk(vk)
{
    SessionState& parent_state = get_session_state(parent_handle).value();
    device = parent_state.graphics_binding.device;
    queue = parent_state.queue;
}

XrResult SwapchainState::acquire(uint32_t& index_out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (num_acquired >= images.size()) {
        // all images are already acquired
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    SwapchainImage& swapchain_image = images[acquire_head];

    if (is_static && swapchain_image.has_been_acquired) {
        // you can only acquire the one image in a static swapchain once
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    // enqueue command buffer to re-acquire image, transition layout, and signal fence

    // make the pipeline barrier in the command buffer wait for the semaphore
    VkPipelineStageFlags wait_flags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &swapchain_image.acquire_command_buffer;
    if (swapchain_image.has_been_acquired) {
        // don't wait on the semaphore the first time
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &swapchain_image.copying_done;
        submit_info.pWaitDstStageMask = &wait_flags;
    }
    else {
        // wait on the semaphore next time
        swapchain_image.has_been_acquired = true;
    }

    VkResult result = vk.QueueSubmit(queue, 1, &submit_info, swapchain_image.copying_done_fence);
    if (result != VK_SUCCESS) {
        spdlog::error("Failed to submit image acquire command buffer: {}", (int)result);
        return XR_ERROR_RUNTIME_FAILURE;
    }

    index_out = acquire_head;
    acquire_head = (acquire_head + 1) % images.size();
    num_acquired += 1;
    return XR_SUCCESS;
}

XrResult SwapchainState::wait(XrDuration timeout) {
    std::unique_lock<std::mutex> lock(mutex);

    if (num_acquired == 0) {
        // no image has been acquired to wait on
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    if (wait_head != release_head) {
        // every wait call must be followed by a release call.
        // in other words, the wait head cannot be more than one slot ahead of the release head
        // by the end of this function.
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    SwapchainImage& swapchain_image = images[wait_head];

    VkResult result = vk.WaitForFences(
        device,
        1, &swapchain_image.copying_done_fence,
        VK_TRUE,
        timeout
    );

    if (result == VK_TIMEOUT) {
        return XR_TIMEOUT_EXPIRED;
    }
    else if (result != VK_SUCCESS) {
        spdlog::error("Failed to wait for swapchain image fence: {}", (int)result);
        return XR_ERROR_RUNTIME_FAILURE;
    }
    else {
        wait_head = (wait_head + 1) % images.size();
        return XR_SUCCESS;
    }
}

XrResult SwapchainState::release(uint32_t& index_out) {
    std::lock_guard<std::mutex> lock(mutex);
    if (num_acquired == 0) {
        // no image to release
        return XR_ERROR_CALL_ORDER_INVALID;
    }
    if (wait_head != (release_head + 1) % images.size()) {
        // the current image has not been waited on
        return XR_ERROR_CALL_ORDER_INVALID;
    }

    VkResult result{};

    SwapchainImage& swapchain_image = images[release_head];

    // we reset this fence in release instead of acquire because it should stay signaled until the
    // first release (shouldn't wait if we've never released).
    result = vk.ResetFences(device, 1, &swapchain_image.copying_done_fence);
    if (result != VK_SUCCESS) {
        spdlog::error("Failed to reset image fence: {}", (int)result);
        return XR_ERROR_RUNTIME_FAILURE;
    }

    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &swapchain_image.release_command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &swapchain_image.rendering_done;

    result = vk.QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        spdlog::error("Failed to submit image release command buffer: {}", (int)result);
        return XR_ERROR_RUNTIME_FAILURE;
    }

    index_out = release_head;
    release_head = (release_head + 1) % images.size();
    num_acquired -= 1;
    return XR_SUCCESS;
}

SessionState::SessionState(
    XrSession handle,
    const XrGraphicsBindingVulkan2KHR& graphics_binding,
    VkQueue queue,
    VkCommandPool command_pool
)
    : handle(handle),
    graphics_binding(graphics_binding),
    queue(queue),
    command_pool(command_pool)
{}
