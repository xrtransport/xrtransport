// SPDX-License-Identifier: LGPL-3.0-or-later

#include "vulkan_common.h"
#include "vulkan_loader.h"
#include "vulkan_init.h"
#include "image_handles.h"
#include "image_type.h"
#include "vulkan_server_module_factory.h"

#include "xrtransport/server/module_interface.h"
#include "xrtransport/server/module_signature.h"

#include "xrtransport/transport/transport.h"
#include "xrtransport/serialization/serializer.h"
#include "xrtransport/serialization/deserializer.h"

#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
#include "vulkan_boxed_handles.h"
using namespace gfxstream::host::vk;
#else
#include "xrtransport/handle_exchange.h"
#endif

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

#include <spdlog/spdlog.h>

#include <string>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <tuple>
#include <unordered_set>
#include <vector>
#include <optional>

using namespace xrtransport;

namespace {

//// Swapchain and Session state structs ////

struct SharedImage {
    VkImage image;
    VkDeviceMemory shared_memory;
    VkSemaphore rendering_done;
    VkSemaphore copying_done;
    VkCommandBuffer command_buffer;
    VkFence command_buffer_fence;

    VkImageAspectFlags aspect;
    uint32_t num_levels;
    uint32_t num_layers;

    VkImageCreateInfo create_info;
    uint64_t memory_size;
    uint32_t memory_type_index;
};

struct RuntimeImage {
    VkImage image;
};

// used to store the results of exporting a SharedImage and reimporting into another VkDevice
struct ImportedImage {
    VkImage image;
    VkDeviceMemory memory;
    VkSemaphore rendering_done;
    VkSemaphore copying_done;
};

struct SwapchainState {
    XrSwapchain handle;
    XrSession parent_handle;
    std::vector<SharedImage> shared_images;
    std::vector<RuntimeImage> runtime_images;
    ImageType image_type;
    uint32_t width;
    uint32_t height;

    explicit SwapchainState(
        XrSwapchain handle,
        XrSession parent_handle,
        std::vector<SharedImage>&& shared_images,
        std::vector<RuntimeImage>&& runtime_images,
        ImageType image_type,
        uint32_t width,
        uint32_t height
    ) :
        handle(handle),
        parent_handle(parent_handle),
        shared_images(std::move(shared_images)),
        runtime_images(std::move(runtime_images)),
        image_type(image_type),
        width(width),
        height(height)
    {}
};

struct SessionState {
    XrSession handle;
    std::unordered_set<XrSwapchain> swapchains;

    explicit SessionState(XrSession handle)
        : handle(handle)
    {}
};

//// Main server module ////

class VulkanServerModule : public ServerModule {
private:
    Transport transport;
    FunctionLoader& function_loader;
    std::unique_ptr<VulkanLoader> vk =
        std::make_unique<VulkanLoader>(VulkanLoader::init_global(get_vulkan_init_function()));

    std::unordered_map<XrSwapchain, SwapchainState> swapchain_states;
    std::unordered_map<XrSession, SessionState> session_states;

    XrInstance saved_xr_instance = XR_NULL_HANDLE;
    XrSystemId saved_xr_system_id = 0;

    VkInstance saved_vk_instance = VK_NULL_HANDLE;
    VkPhysicalDevice saved_vk_physical_device = VK_NULL_HANDLE;
    VkDevice saved_vk_device = VK_NULL_HANDLE;

    uint32_t queue_family_index = 0;
    uint32_t queue_index = 0;
    VkQueue saved_vk_queue = VK_NULL_HANDLE;

    VkCommandPool saved_vk_command_pool = VK_NULL_HANDLE;

    uint8_t physical_device_uuid[VK_UUID_SIZE]{};

    PFN_xrGetVulkanGraphicsRequirements2KHR pfn_xrGetVulkanGraphicsRequirements2KHR = nullptr;
    PFN_xrCreateVulkanInstanceKHR pfn_xrCreateVulkanInstanceKHR = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR pfn_xrGetVulkanGraphicsDevice2KHR = nullptr;
    PFN_xrCreateVulkanDeviceKHR pfn_xrCreateVulkanDeviceKHR = nullptr;

    //// Session and Swapchain storage helpers ////

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
        std::vector<SharedImage>&& shared_images,
        std::vector<RuntimeImage>&& runtime_images,
        ImageType image_type,
        uint32_t width,
        uint32_t height
    ) {
        return swapchain_states.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(handle),
            std::forward_as_tuple(
                handle,
                parent_handle,
                std::move(shared_images),
                std::move(runtime_images),
                image_type,
                width,
                height
            )
        ).first->second;
    }

    SessionState& store_session_state(
        XrSession handle
    ) {
        return session_states.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(handle),
            std::forward_as_tuple(
                handle
            )
        ).first->second;
    }

    void destroy_swapchain_state(XrSwapchain handle) {
        swapchain_states.erase(handle);
    }

    void destroy_session_state(XrSession handle) {
        session_states.erase(handle);
    }

    //// Vulkan Helpers ////

    void select_queue_family() {
        uint32_t queue_family_count{};
        vk->GetPhysicalDeviceQueueFamilyProperties(saved_vk_physical_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
        vk->GetPhysicalDeviceQueueFamilyProperties(saved_vk_physical_device, &queue_family_count, queue_families.data());

        bool queue_family_found = false;
        for (size_t i = 0; i < queue_families.size(); i++) {
            auto& queue_family = queue_families[i];

            // need at least one queue
            if (queue_family.queueCount < 1) {
                continue;
            }

            // need transfer, or graphics (which includes transfer commands)
            if (queue_family.queueFlags & VK_QUEUE_TRANSFER_BIT || queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queue_family_found = true;
                queue_family_index = i;
                queue_index = 0;
            }
        }

        if (!queue_family_found) {
            throw std::runtime_error("Unable to find a queue family with at least one queue "
                "and that supports transfer operations");
        }
    }

    void setup_vulkan_instance() {
        XrResult xr_result{};
        VkResult vk_result{};

        XrSystemGetInfo system_get_info{XR_TYPE_SYSTEM_GET_INFO};
        system_get_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

        xr_result = function_loader.GetSystem(saved_xr_instance, &system_get_info, &saved_xr_system_id);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Failed to get HMD system id: " + std::to_string(xr_result));
        }

        // unused but required by spec
        XrGraphicsRequirementsVulkan2KHR graphics_requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};

        xr_result = pfn_xrGetVulkanGraphicsRequirements2KHR(saved_xr_instance, saved_xr_system_id, &graphics_requirements);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Failed to get Vulkan graphics requirements: " + std::to_string(xr_result));
        }

        VkApplicationInfo vk_application_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        vk_application_info.pApplicationName = "xrtransport server";
        vk_application_info.applicationVersion = 1;
        vk_application_info.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo vk_create_info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        vk_create_info.pApplicationInfo = &vk_application_info;
#ifndef NDEBUG
        const char* layers[]{"VK_LAYER_KHRONOS_validation"};
        vk_create_info.enabledLayerCount = 1;
        vk_create_info.ppEnabledLayerNames = layers;
#endif

        XrVulkanInstanceCreateInfoKHR xr_create_info{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
        xr_create_info.systemId = saved_xr_system_id;
        xr_create_info.pfnGetInstanceProcAddr = vk->GetInstanceProcAddr;
        xr_create_info.vulkanCreateInfo = &vk_create_info;

        xr_result = pfn_xrCreateVulkanInstanceKHR(saved_xr_instance, &xr_create_info, &saved_vk_instance, &vk_result);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Vulkan error on instance creation: " + std::to_string(vk_result));
        }
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("XR error on Vulkan instance creation: " + std::to_string(xr_result));
        }

        // fetch instance-level Vulkan functions
        vk = std::make_unique<VulkanLoader>(vk->init_instance(saved_vk_instance));

        XrVulkanGraphicsDeviceGetInfoKHR xr_device_get_info{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
        xr_device_get_info.systemId = saved_xr_system_id;
        xr_device_get_info.vulkanInstance = saved_vk_instance;

        xr_result = pfn_xrGetVulkanGraphicsDevice2KHR(saved_xr_instance, &xr_device_get_info, &saved_vk_physical_device);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Failed to get Vulkan graphics device: " + std::to_string(xr_result));
        }

        // Save PhysicalDevice UUID to send to client
        VkPhysicalDeviceIDProperties vk_device_id_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};

        VkPhysicalDeviceProperties2 vk_device_props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        vk_device_props.pNext = &vk_device_id_props;

        vk->GetPhysicalDeviceProperties2(saved_vk_physical_device, &vk_device_props);

        std::memcpy(physical_device_uuid, vk_device_id_props.deviceUUID, VK_UUID_SIZE);

        // choose queue family
        select_queue_family();

        // Setup VkDevice
        const char* vk_device_extensions[]{
#ifdef _WIN32
            VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
#else
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
#endif
        };

        float queue_priority = 1.0;

        VkDeviceQueueCreateInfo vk_queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        vk_queue_info.queueFamilyIndex = queue_family_index;
        vk_queue_info.queueCount = 1;
        vk_queue_info.pQueuePriorities = &queue_priority;

        VkDeviceCreateInfo vk_device_create_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        vk_device_create_info.queueCreateInfoCount = 1;
        vk_device_create_info.pQueueCreateInfos = &vk_queue_info;
        vk_device_create_info.enabledExtensionCount = sizeof(vk_device_extensions) / sizeof(const char*);
        vk_device_create_info.ppEnabledExtensionNames = vk_device_extensions;

        XrVulkanDeviceCreateInfoKHR xr_device_create_info{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
        xr_device_create_info.systemId = saved_xr_system_id;
        xr_device_create_info.pfnGetInstanceProcAddr = vk->GetInstanceProcAddr;
        xr_device_create_info.vulkanPhysicalDevice = saved_vk_physical_device;
        xr_device_create_info.vulkanCreateInfo = &vk_device_create_info;

        xr_result = pfn_xrCreateVulkanDeviceKHR(saved_xr_instance, &xr_device_create_info, &saved_vk_device, &vk_result);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Vulkan error on device creation: " + std::to_string(vk_result));
        }
        if (xr_result != XR_SUCCESS) {
            throw std::runtime_error("XR error on Vulkan device creation: " + std::to_string(xr_result));
        }

        // initialize device-level Vulkan functions
        vk = std::make_unique<VulkanLoader>(vk->init_device(saved_vk_device));

        vk->GetDeviceQueue(saved_vk_device, queue_family_index, queue_index, &saved_vk_queue);

        // Create command pool
        VkCommandPoolCreateInfo pool_create_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_create_info.flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_create_info.queueFamilyIndex = queue_family_index;

        vk_result = vk->CreateCommandPool(saved_vk_device, &pool_create_info, nullptr, &saved_vk_command_pool);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool: " + std::to_string(vk_result));
        }
    }

    int32_t find_memory_type(
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        uint32_t memory_type_bits_requirement,
        VkMemoryPropertyFlags required_flags)
    {
        const uint32_t memory_count = memory_properties.memoryTypeCount;
        for (uint32_t memory_index = 0; memory_index < memory_count; memory_index++) {
            if (!(memory_type_bits_requirement & (1 << memory_index)))
                continue;

            VkMemoryPropertyFlags flags = memory_properties.memoryTypes[memory_index].propertyFlags;

            if ((flags & required_flags) == required_flags)
                return static_cast<int64_t>(memory_index);
        }

        // failed to find memory type
        return -1;
    }

    VkSemaphore create_exportable_semaphore() {
        VkResult result{};

        VkExportSemaphoreCreateInfo export_info{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
#ifdef _WIN32
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkSemaphoreCreateInfo create_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        create_info.pNext = &export_info;

        VkSemaphore semaphore{};
        result = vk->CreateSemaphore(saved_vk_device, &create_info, nullptr, &semaphore);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Unable to create exportable semaphore: " + std::to_string(result));
        }

        return semaphore;
    }

    VkCommandBuffer create_command_buffer() {
        VkCommandBufferAllocateInfo cmdbuf_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdbuf_info.commandPool = saved_vk_command_pool;
        cmdbuf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdbuf_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer{};
        VkResult result = vk->AllocateCommandBuffers(saved_vk_device, &cmdbuf_info, &command_buffer);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Unable to allocate command buffer: " + std::to_string(result));
        }

        return command_buffer;
    }

    VkFence create_signaled_fence() {
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkFence fence{};
        VkResult result = vk->CreateFence(saved_vk_device, &fence_info, nullptr, &fence);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Unable to create fence: " + std::to_string(result));
        }

        return fence;
    }

    xrtp_Handle export_memory(VkDeviceMemory memory) {
        VkResult vk_result{};

        xrtp_Handle image_handle{};

#ifdef _WIN32
        #error TODO
#else
        VkMemoryGetFdInfoKHR get_fd_info{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
        get_fd_info.memory = memory;
        get_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

        int fd{};
        vk_result = vk->GetMemoryFdKHR(saved_vk_device, &get_fd_info, &fd);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to get memory fd: " + std::to_string(vk_result));
        }

        image_handle = static_cast<xrtp_Handle>(fd);
#endif

        return image_handle;
    }

    xrtp_Handle export_semaphore(VkSemaphore semaphore) {
        VkResult vk_result{};

        xrtp_Handle handle{};

#ifdef _WIN32
        #error TODO
#else
        VkSemaphoreGetFdInfoKHR get_fd_info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
        get_fd_info.semaphore = semaphore;
        get_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

        int fd{};
        vk_result = vk->GetSemaphoreFdKHR(saved_vk_device, &get_fd_info, &fd);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Unable to export FD for semaphore: " + std::to_string(vk_result));
        }

        handle = static_cast<xrtp_Handle>(fd);
#endif

        return handle;
    }

    ImageHandles export_image_handles(const SharedImage& shared_image) {
        ImageHandles result{};
        result.memory_handle = export_memory(shared_image.shared_memory);
        result.rendering_done_handle = export_semaphore(shared_image.rendering_done);
        result.copying_done_handle = export_semaphore(shared_image.copying_done);

        return result;
    }

    VkSemaphore import_semaphore(VkDevice device, xrtp_Handle handle) {
        VkResult result{};

        VkSemaphoreCreateInfo create_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};

        VkSemaphore semaphore{};
        result = vk->CreateSemaphore(device, &create_info, nullptr, &semaphore);
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

        result = vk->ImportSemaphoreFdKHR(device, &import_info);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to import semaphore: " + std::to_string(result));
        }
    #endif

        return semaphore;
    }

    // used to share image and semaphores into another device so that we can send Vulkan handles directly
    // back to the client (gfxstream build)
    ImportedImage import_image_into_device(const SharedImage& shared_image, VkDevice device) {
        VkResult vk_result{};

        ImageHandles image_handles = export_image_handles(shared_image);

        VkExternalMemoryImageCreateInfo external_create_info{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
#ifdef _WIN32
        external_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        external_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
        
        // copy saved VkImageCreateInfo
        VkImageCreateInfo vk_create_info = shared_image.create_info;

        vk_create_info.pNext = &external_create_info;
        vk_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        VkImageAspectFlags aspect = get_aspect_from_format(vk_create_info.format);
        uint32_t num_levels = vk_create_info.mipLevels;
        uint32_t num_layers = vk_create_info.arrayLayers;

        VkImage image{};
        vk_result = vk->CreateImage(device, &vk_create_info, nullptr, &image);
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
        alloc_info.allocationSize = shared_image.memory_size;
        alloc_info.memoryTypeIndex = shared_image.memory_type_index;

        VkDeviceMemory image_memory{};
        vk_result = vk->AllocateMemory(device, &alloc_info, nullptr, &image_memory);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to import memory from FD: " + std::to_string(vk_result));
        }

        vk_result = vk->BindImageMemory(device, image, image_memory, 0);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind memory to image: " + std::to_string(vk_result));
        }

#ifdef _WIN32
        #error Close handle if needed
#else
        // No need to close the FD, the driver closes it upon successful import.
#endif

        VkSemaphore rendering_done = import_semaphore(device, image_handles.rendering_done_handle);
        VkSemaphore copying_done = import_semaphore(device, image_handles.copying_done_handle);

        return ImportedImage{
            .image = image,
            .memory = image_memory,
            .rendering_done = rendering_done,
            .copying_done = copying_done
        };
    }

    std::tuple<SharedImage, uint64_t, uint32_t> create_image(
        const XrSwapchainCreateInfo& create_info,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkMemoryPropertyFlags required_flags
    ) {
        VkResult vk_result{};

        VkExternalMemoryImageCreateInfo external_image_create_info{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
#ifdef _WIN32
        external_image_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        external_image_create_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkImageCreateInfo image_create_info = create_vk_image_create_info(create_info);
        image_create_info.pNext = &external_image_create_info;
        image_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        VkImageAspectFlags aspect = get_aspect_from_format(image_create_info.format);
        uint32_t num_levels = image_create_info.mipLevels;
        uint32_t num_layers = image_create_info.arrayLayers;

        VkImage image{};
        vk_result = vk->CreateImage(saved_vk_device, &image_create_info, nullptr, &image);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Unable to create VkImage: " + std::to_string(vk_result));
        }

        VkMemoryRequirements memory_requirements{};
        vk->GetImageMemoryRequirements(saved_vk_device, image, &memory_requirements);

        int32_t memory_type = find_memory_type(memory_properties, memory_requirements.memoryTypeBits, required_flags);
        if (memory_type == -1) {
            throw std::runtime_error("Unable to find memory type with required bits: " +
                std::to_string(memory_requirements.memoryTypeBits));
        }
        uint32_t memory_type_index = static_cast<uint32_t>(memory_type);

        VkMemoryDedicatedAllocateInfo dedicated_alloc_info{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
        dedicated_alloc_info.image = image;

        VkExportMemoryAllocateInfo export_alloc_info{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
        export_alloc_info.pNext = &dedicated_alloc_info;
#ifdef _WIN32
        export_alloc_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
        export_alloc_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

        VkMemoryAllocateInfo alloc_info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc_info.pNext = &export_alloc_info;
        alloc_info.allocationSize = memory_requirements.size;
        alloc_info.memoryTypeIndex = memory_type_index;

        VkDeviceMemory memory{};
        vk_result = vk->AllocateMemory(saved_vk_device, &alloc_info, nullptr, &memory);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate swapchain memory: " + std::to_string(vk_result));
        }

        vk_result = vk->BindImageMemory(saved_vk_device, image, memory, 0);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to bind memory to image: " + std::to_string(vk_result));
        }

        auto rendering_done = create_exportable_semaphore();
        auto copying_done = create_exportable_semaphore();

        auto command_buffer = create_command_buffer();
        auto command_buffer_fence = create_signaled_fence();

        return {
            SharedImage{
                .image = image,
                .shared_memory = memory,
                .rendering_done = rendering_done,
                .copying_done = copying_done,
                .command_buffer = command_buffer,
                .command_buffer_fence = command_buffer_fence,
                .aspect = aspect,
                .num_levels = num_levels,
                .num_layers = num_layers,
                .create_info = image_create_info,
                .memory_size = alloc_info.allocationSize,
                .memory_type_index = alloc_info.memoryTypeIndex
            },
            memory_requirements.size,
            memory_type_index
        };
    }

    SwapchainState& create_swapchain_state(
        SessionState& session_state,
        const XrSwapchainCreateInfo& create_info,
        XrSwapchain handle,
        uint64_t& memory_size_out,
        uint32_t& memory_type_index_out
    ) {
        VkResult vk_result{};
        XrResult xr_result{};

        uint32_t num_images{};
        xr_result = function_loader.EnumerateSwapchainImages(handle, 0, &num_images, nullptr);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Unable to get swapchain images: " + std::to_string(xr_result));
        }

        std::vector<XrSwapchainImageVulkan2KHR> runtime_image_structs(num_images, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR});
        auto p_runtime_image_structs = reinterpret_cast<XrSwapchainImageBaseHeader*>(runtime_image_structs.data());
        xr_result = function_loader.EnumerateSwapchainImages(handle, num_images, &num_images, p_runtime_image_structs);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Unable to get swapchain images: " + std::to_string(xr_result));
        }

        std::vector<SharedImage> shared_images;
        shared_images.reserve(num_images);
        std::vector<RuntimeImage> runtime_images;
        runtime_images.reserve(num_images);

        VkPhysicalDeviceMemoryProperties memory_properties{};
        vk->GetPhysicalDeviceMemoryProperties(saved_vk_physical_device, &memory_properties);

        VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (create_info.createFlags & XR_SWAPCHAIN_CREATE_PROTECTED_CONTENT_BIT) {
            required_flags |= VK_MEMORY_PROPERTY_PROTECTED_BIT;
        }

        bool is_static = (create_info.createFlags & XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT) != 0;

        for (uint32_t i = 0; i < num_images; i++) {
            auto [shared_image, memory_size, memory_type_index] = create_image(
                create_info,
                memory_properties,
                required_flags
            );

            shared_images.emplace_back(std::move(shared_image));

            runtime_images.emplace_back(RuntimeImage{runtime_image_structs[i].image});

            // just overwrite these values for each image because they should be the same
            assert(i == 0 || (memory_size_out == memory_size && memory_type_index_out == memory_type_index));
            memory_size_out = memory_size;
            memory_type_index_out = memory_type_index;
        }

        ImageType image_type;
        if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT) {
            image_type = ImageType::COLOR;
        }
        else if (create_info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            image_type = ImageType::DEPTH_STENCIL;
        }
        else {
            throw std::runtime_error("Images must be either color or depth-stencil images");
        }

        SwapchainState& result = store_swapchain_state(
            handle,
            session_state.handle,
            std::move(shared_images),
            std::move(runtime_images),
            image_type,
            create_info.width,
            create_info.height
        );

        session_state.swapchains.emplace(handle);
        return result;
    }

    //// Message Handlers ////

    void handle_get_physical_device(MessageLockIn msg_in) {
        // no data to read
        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_RETURN_PHYSICAL_DEVICE);
        SerializeContext s_ctx(msg_out.buffer);
        serialize_array(physical_device_uuid, VK_UUID_SIZE, s_ctx);
        msg_out.flush();
    }

    void handle_create_swapchain(MessageLockIn msg_in) {
        XrSession session_handle{};
        XrSwapchainCreateInfo* create_info{};

        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&session_handle, d_ctx);
        deserialize_ptr(&create_info, d_ctx);

        // add transfer destination usage (only for runtime swapchain)
        XrSwapchainUsageFlags old_flags = create_info->usageFlags;
        create_info->usageFlags |= XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;

        XrSwapchain swapchain_handle{};
        XrResult result = function_loader.CreateSwapchain(session_handle, create_info, &swapchain_handle);
        if (result != XR_SUCCESS) {
            spdlog::error("Failed to create native swapchain: {}", (int)result);
            auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_CREATE_SWAPCHAIN_RETURN);
            SerializeContext s_ctx(msg_out.buffer);
            serialize(&result, s_ctx);
            msg_out.flush();
            return;
        }

        // restore usage flags (we don't want to create shared images with TRANSFER_DST_BIT)
        create_info->usageFlags = old_flags;

        // Create corresponding swapchain and send memory and semaphore handles over handle exchange

        SessionState& session_state = get_session_state(session_handle).value();

        uint64_t memory_size{};
        uint32_t memory_type_index{};

        SwapchainState& swapchain_state = create_swapchain_state(
            session_state,
            *create_info,
            swapchain_handle,
            memory_size,
            memory_type_index
        );

        // gfxstream build doesn't use handle exchange
#ifndef XRTRANSPORT_BUILD_FOR_GFXSTREAM
        for (const auto& shared_image : swapchain_state.shared_images) {
            auto image_handles = export_image_handles(shared_image);

            // xrtp_write_handle should take care of closing our copy of the handle
            write_image_handles(image_handles);
        }
#endif

        uint32_t num_images = static_cast<uint32_t>(swapchain_state.shared_images.size());

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_CREATE_SWAPCHAIN_RETURN);
        SerializeContext s_ctx(msg_out.buffer);
        serialize(&result, s_ctx);
        serialize(&swapchain_handle, s_ctx);
        serialize(&num_images, s_ctx);
        serialize(&memory_size, s_ctx);
        serialize(&memory_type_index, s_ctx);
        msg_out.flush();

        cleanup_ptr(create_info, 1);
    }

    void destroy_swapchain(XrSwapchain swapchain_handle) {
        SwapchainState& swapchain_state = get_swapchain_state(swapchain_handle).value();
        SessionState& session_state = get_session_state(swapchain_state.parent_handle).value();

        vk->QueueWaitIdle(saved_vk_queue);

        for (auto& image : swapchain_state.shared_images) {
            vk->FreeCommandBuffers(
                saved_vk_device,
                saved_vk_command_pool,
                1,
                &image.command_buffer
            );
            vk->DestroyImage(saved_vk_device, image.image, nullptr);
            vk->FreeMemory(saved_vk_device, image.shared_memory, nullptr);
            vk->DestroySemaphore(saved_vk_device, image.rendering_done, nullptr);
            vk->DestroySemaphore(saved_vk_device, image.copying_done, nullptr);
        }

        destroy_swapchain_state(swapchain_handle);
        session_state.swapchains.erase(swapchain_handle);

        function_loader.DestroySwapchain(swapchain_handle);
    }

    void handle_destroy_swapchain(MessageLockIn msg_in) {
        XrSwapchain swapchain_handle{};

        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&swapchain_handle, d_ctx);

        destroy_swapchain(swapchain_handle);

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_DESTROY_SWAPCHAIN_RETURN);
        msg_out.flush();
    }

    void handle_create_session(MessageLockIn msg_in) {
        // We don't need the client's graphics binding, and we're only using the HMD system id, so
        // we only need to get the create flags from the client.
        XrSessionCreateFlags flags{};
        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&flags, d_ctx);

        XrGraphicsBindingVulkan2KHR graphics_binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
        graphics_binding.instance = saved_vk_instance;
        graphics_binding.physicalDevice = saved_vk_physical_device;
        graphics_binding.device = saved_vk_device;
        graphics_binding.queueFamilyIndex = queue_family_index;
        graphics_binding.queueIndex = queue_index;

        XrSessionCreateInfo create_info{XR_TYPE_SESSION_CREATE_INFO};
        create_info.next = &graphics_binding;
        create_info.createFlags = flags;
        create_info.systemId = saved_xr_system_id;

        XrSession session_handle{};
        XrResult result = function_loader.CreateSession(saved_xr_instance, &create_info, &session_handle);
        if (!XR_SUCCEEDED(result)) {
            throw std::runtime_error("Failed to create XR session: " + std::to_string(result));
        }

        store_session_state(session_handle);

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_CREATE_SESSION_RETURN);
        SerializeContext s_ctx(msg_out.buffer);
        serialize(&session_handle, s_ctx);
        msg_out.flush();
    }

    void handle_destroy_session(MessageLockIn msg_in) {
        XrSession session_handle{};

        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&session_handle, d_ctx);

        SessionState& session_state = get_session_state(session_handle).value();

        for (XrSwapchain swapchain_handle : session_state.swapchains) {
            // destroys extra images and semaphores along with the XR swapchain
            destroy_swapchain(swapchain_handle);
        }

        function_loader.DestroySession(session_handle);

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_DESTROY_SESSION_RETURN);
        msg_out.flush();
    }

    void handle_release_swapchain_image(MessageLockIn msg_in) {
        XrResult xr_result{};
        VkResult vk_result{};

        XrSwapchain swapchain_handle{};
        uint32_t src_index{};

        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&swapchain_handle, d_ctx);
        deserialize(&src_index, d_ctx);

        SwapchainState& swapchain_state = get_swapchain_state(swapchain_handle).value();

        uint32_t dest_index{};
        xr_result = function_loader.AcquireSwapchainImage(swapchain_handle, nullptr, &dest_index);

        auto& shared_image = swapchain_state.shared_images.at(src_index);
        auto& runtime_image = swapchain_state.runtime_images.at(dest_index);

        VkImage src_image = shared_image.image;
        VkImage dest_image = runtime_image.image;
        VkSemaphore rendering_done = shared_image.rendering_done;
        VkSemaphore copying_done = shared_image.copying_done;
        VkCommandBuffer command_buffer = shared_image.command_buffer;
        VkFence command_buffer_fence = shared_image.command_buffer_fence;
        VkImageAspectFlags aspect = shared_image.aspect;
        uint32_t num_layers = shared_image.num_layers;

        // wait on fence to make sure that the command buffer is not still being used
        // synchronization with the client should already guarantee this, but this is just for safety
        // in case of misbehaving clients
        vk_result = vk->WaitForFences(saved_vk_device, 1, &command_buffer_fence, VK_TRUE, UINT64_MAX);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to wait on fence: " + std::to_string(vk_result));
        }
        vk_result = vk->ResetFences(saved_vk_device, 1, &command_buffer_fence);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to reset fence: " + std::to_string(vk_result));
        }

        // record command buffer with these source and destination images

        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vk_result = vk->BeginCommandBuffer(command_buffer, &begin_info);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to begin command buffer: " + std::to_string(vk_result));
        }

        // initialize these values that will be reused later. represents the first mip and all layers

        VkImageSubresourceRange image_subresource_range{};
        image_subresource_range.aspectMask = aspect;
        image_subresource_range.baseMipLevel = 0;
        image_subresource_range.levelCount = 1;
        image_subresource_range.baseArrayLayer = 0;
        image_subresource_range.layerCount = num_layers;

        VkImageSubresourceLayers image_subresource_layers{};
        image_subresource_layers.aspectMask = aspect;
        image_subresource_layers.mipLevel = 0;
        image_subresource_layers.baseArrayLayer = 0;
        image_subresource_layers.layerCount = num_layers;

        std::array<VkImageMemoryBarrier, 2> image_barriers{{
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER},
            {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}
        }};

        auto& src_acquire_barrier = image_barriers[0];
        src_acquire_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // TODO: need to read this from the client because it could be GENERAL if the client is OpenGL
        src_acquire_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        src_acquire_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        src_acquire_barrier.dstQueueFamilyIndex = queue_family_index;
        src_acquire_barrier.image = src_image;
        src_acquire_barrier.subresourceRange = image_subresource_range;

        auto& dest_transition_barrier = image_barriers[1];
        dest_transition_barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dest_transition_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dest_transition_barrier.image = dest_image;
        dest_transition_barrier.subresourceRange = image_subresource_range;

        vk->CmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            2, image_barriers.data()
        );

        VkImageCopy image_copy{};
        image_copy.srcSubresource = image_subresource_layers; // first mip and all layers
        image_copy.dstSubresource = image_subresource_layers;
        image_copy.srcOffset = {0, 0, 0};
        image_copy.dstOffset = {0, 0, 0};
        // TODO: it's not clear if the depth value of this extent applies to image layers. If only one
        // eye ends up displaying, this would be a good place to look.
        image_copy.extent = {swapchain_state.width, swapchain_state.height, 1};

        vk->CmdCopyImage(
            command_buffer,
            src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dest_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &image_copy
        );

        VkImageMemoryBarrier dest_transition_back_barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        dest_transition_back_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dest_transition_back_barrier.newLayout =
            swapchain_state.image_type == ImageType::COLOR ?
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        dest_transition_back_barrier.image = dest_image;
        dest_transition_back_barrier.subresourceRange = image_subresource_range;

        vk->CmdPipelineBarrier(
            command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &dest_transition_back_barrier
        );

        vk_result = vk->EndCommandBuffer(command_buffer);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to end command buffer: " + std::to_string(vk_result));
        }

        // command buffer recorded, now use it

        XrSwapchainImageWaitInfo wait_info{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait_info.timeout = XR_INFINITE_DURATION;
        xr_result = function_loader.WaitSwapchainImage(swapchain_handle, &wait_info);
        if (xr_result != XR_SUCCESS) {
            throw std::runtime_error("Failed to wait for swapchain image: " + std::to_string(xr_result));
        }

        VkPipelineStageFlags all_commands_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &rendering_done;
        submit_info.pWaitDstStageMask = &all_commands_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &copying_done;

        vk_result = vk->QueueSubmit(saved_vk_queue, 1, &submit_info, command_buffer_fence);
        if (vk_result != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit copy operation to queue: " + std::to_string(vk_result));
        }

        xr_result = function_loader.ReleaseSwapchainImage(swapchain_handle, nullptr);
        if (!XR_SUCCEEDED(xr_result)) {
            throw std::runtime_error("Failed to release swapchain image: " + std::to_string(xr_result));
        }

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_RELEASE_SWAPCHAIN_IMAGE_RETURN);
        msg_out.flush();
    }

    void handle_get_swapchain_image_handles(MessageLockIn msg_in) {
        VkDevice boxed_vk_device{};
        XrSwapchain swapchain_handle{};

        DeserializeContext d_ctx(msg_in.buffer);
        deserialize(&boxed_vk_device, d_ctx);
        deserialize(&swapchain_handle, d_ctx);

#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
        uint32_t ok = 1;

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_RETURN_SWAPCHAIN_IMAGE_HANDLES);
        SerializeContext s_ctx(msg_out.buffer);
        serialize(&ok, s_ctx);

        // unbox from guest handle space to real handle
        VkDevice vk_device = unbox_VkDevice(boxed_vk_device);

        SwapchainState& swapchain_state = get_swapchain_state(swapchain_handle).value();
        for (const auto& shared_image : swapchain_state.shared_images) {
            // import image and semaphores into provided VkDevice
            ImportedImage imported_image = import_image_into_device(shared_image, vk_device);

            // box imported handles into guest handle space
            VkImage image = new_boxed_non_dispatchable_VkImage(imported_image.image);
            VkDeviceMemory memory = new_boxed_non_dispatchable_VkDeviceMemory(imported_image.memory);
            VkSemaphore rendering_done = new_boxed_non_dispatchable_VkSemaphore(imported_image.rendering_done);
            VkSemaphore copying_done = new_boxed_non_dispatchable_VkSemaphore(imported_image.copying_done);

            serialize(&image, s_ctx);
            serialize(&memory, s_ctx);
            serialize(&rendering_done, s_ctx);
            serialize(&copying_done, s_ctx);
        }

        msg_out.flush();

#else // XRTRANSPORT_BUILD_FOR_GFXSTREAM
        // we don't allow this command on the default build
        uint32_t ok = 0;

        auto msg_out = transport.start_message(XRTP_MSG_VULKAN2_RETURN_SWAPCHAIN_IMAGE_HANDLES);
        SerializeContext s_ctx(msg_out.buffer);
        serialize(&ok, s_ctx);
        msg_out.flush();
#endif
    }

public:
    VulkanServerModule(xrtp_Transport transport_handle, FunctionLoader* p_function_loader)
    : transport(transport_handle),
    function_loader(*p_function_loader) {
        transport.register_handler(
            XRTP_MSG_VULKAN2_GET_PHYSICAL_DEVICE,
            [this](MessageLockIn msg_in) {
                handle_get_physical_device(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_CREATE_SWAPCHAIN,
            [this](MessageLockIn msg_in) {
                handle_create_swapchain(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_DESTROY_SWAPCHAIN,
            [this](MessageLockIn msg_in) {
                handle_destroy_swapchain(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_CREATE_SESSION,
            [this](MessageLockIn msg_in) {
                handle_create_session(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_DESTROY_SESSION,
            [this](MessageLockIn msg_in) {
                handle_destroy_session(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_RELEASE_SWAPCHAIN_IMAGE,
            [this](MessageLockIn msg_in) {
                handle_release_swapchain_image(std::move(msg_in));
            });
        transport.register_handler(
            XRTP_MSG_VULKAN2_GET_SWAPCHAIN_IMAGE_HANDLES,
            [this](MessageLockIn msg_in) {
                handle_get_swapchain_image_handles(std::move(msg_in));
            });
    }

    void get_required_extensions(uint32_t* num_extensions_out, const char** extensions_out) const override {
        *num_extensions_out = 1;
        if (extensions_out) {
            extensions_out[0] = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME;
        }
    }

    void on_instance(XrInstance instance) override {
        saved_xr_instance = instance;

        function_loader.ensure_function_loaded("xrGetVulkanGraphicsRequirements2KHR", pfn_xrGetVulkanGraphicsRequirements2KHR);
        function_loader.ensure_function_loaded("xrCreateVulkanInstanceKHR", pfn_xrCreateVulkanInstanceKHR);
        function_loader.ensure_function_loaded("xrGetVulkanGraphicsDevice2KHR", pfn_xrGetVulkanGraphicsDevice2KHR);
        function_loader.ensure_function_loaded("xrCreateVulkanDeviceKHR", pfn_xrCreateVulkanDeviceKHR);

        function_loader.ensure_function_loaded("xrGetSystem", function_loader.GetSystem);
        function_loader.ensure_function_loaded("xrCreateSwapchain", function_loader.CreateSwapchain);
        function_loader.ensure_function_loaded("xrEnumerateSwapchainImages", function_loader.EnumerateSwapchainImages);
        function_loader.ensure_function_loaded("xrDestroySwapchain", function_loader.DestroySwapchain);
        function_loader.ensure_function_loaded("xrCreateSession", function_loader.CreateSession);
        function_loader.ensure_function_loaded("xrDestroySession", function_loader.DestroySession);
        function_loader.ensure_function_loaded("xrAcquireSwapchainImage", function_loader.AcquireSwapchainImage);
        function_loader.ensure_function_loaded("xrWaitSwapchainImage", function_loader.WaitSwapchainImage);
        function_loader.ensure_function_loaded("xrReleaseSwapchainImage", function_loader.ReleaseSwapchainImage);

        setup_vulkan_instance();
    }

    void on_instance_destroy() override {
        saved_xr_instance = XR_NULL_HANDLE;
        saved_xr_system_id = 0;

        vk->DeviceWaitIdle(saved_vk_device);

        vk->DestroyCommandPool(saved_vk_device, saved_vk_command_pool, nullptr);

        // runtime's xrDestroyInstance is responsible for cleaning up the Vulkan instance and device
        // that we created
    }
};

} // namespace

namespace xrtransport {

std::unique_ptr<ServerModule> VulkanServerModuleFactory::create(
    xrtp_Transport transport,
    FunctionLoader* function_loader,
    uint32_t num_extensions,
    const XrExtensionProperties* extensions) {

    bool vulkan2_found = false;
    for (uint32_t i = 0; i < num_extensions; i++) {
        const auto& extension = extensions[i];
        if (extension.extensionName == std::string(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME)) {
            vulkan2_found = true;
        }
    }

    if (!vulkan2_found) {
        spdlog::warn("XR_KHR_vulkan_enable2 extension not found on host runtime, not enabling Vulkan2 module");
        return nullptr; // don't enable if runtime doesn't support XR_KHR_vulkan_enable2
    }

    return std::make_unique<VulkanServerModule>(transport, function_loader);
}

} // namespace xrtransport

#ifdef XRTRANSPORT_DYNAMIC_MODULES

// dynamic module entry point
ServerModule* xrtp_get_server_module(
    xrtp_Transport transport,
    FunctionLoader* function_loader,
    uint32_t num_extensions,
    const XrExtensionProperties* extensions)
{
    return VulkanServerModuleFactory::create(transport, function_loader, num_extensions, extensions).release();
}

#endif