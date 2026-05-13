// SPDX-License-Identifier: LGPL-3.0-or-later

#include "vulkan_common.h"
#include "vulkan_core.h"
#include "vulkan_client_module_factory.h"

#include "xrtransport/client/module_interface.h"
#include "xrtransport/client/module_signature.h"

#include "xrtransport/transport/transport.h"
#include "xrtransport/serialization/serializer.h"
#include "xrtransport/serialization/deserializer.h"

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

#include <spdlog/spdlog.h>

#include <memory>
#include <cassert>
#include <cstring>
#include <vector>

using namespace xrtransport;

namespace vulkan {

// Instance handler forward declaration
void instance_callback(XrInstance instance, PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr);

// Layer functions
PFN_xrGetVulkanInstanceExtensionsKHR pfn_xrGetVulkanInstanceExtensionsKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanInstanceExtensionsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer);

PFN_xrGetVulkanDeviceExtensionsKHR pfn_xrGetVulkanDeviceExtensionsKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanDeviceExtensionsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer);

PFN_xrGetVulkanGraphicsDeviceKHR pfn_xrGetVulkanGraphicsDeviceKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsDeviceKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    VkInstance                                  vkInstance,
    VkPhysicalDevice*                           vkPhysicalDevice);

PFN_xrGetVulkanGraphicsRequirementsKHR pfn_xrGetVulkanGraphicsRequirementsKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsRequirementsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrGraphicsRequirementsVulkanKHR*            graphicsRequirements);

PFN_xrCreateVulkanInstanceKHR pfn_xrCreateVulkanInstanceKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrCreateVulkanInstanceKHRImpl(
    XrInstance                                  instance,
    const XrVulkanInstanceCreateInfoKHR*        createInfo,
    VkInstance*                                 vulkanInstance,
    VkResult*                                   vulkanResult);

PFN_xrCreateVulkanDeviceKHR pfn_xrCreateVulkanDeviceKHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrCreateVulkanDeviceKHRImpl(
    XrInstance                                  instance,
    const XrVulkanDeviceCreateInfoKHR*          createInfo,
    VkDevice*                                   vulkanDevice,
    VkResult*                                   vulkanResult);

PFN_xrGetVulkanGraphicsDevice2KHR pfn_xrGetVulkanGraphicsDevice2KHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsDevice2KHRImpl(
    XrInstance                                  instance,
    const XrVulkanGraphicsDeviceGetInfoKHR*     getInfo,
    VkPhysicalDevice*                           vulkanPhysicalDevice);

PFN_xrGetVulkanGraphicsRequirements2KHR pfn_xrGetVulkanGraphicsRequirements2KHR_next;
XRAPI_ATTR XrResult XRAPI_CALL xrGetVulkanGraphicsRequirements2KHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrGraphicsRequirementsVulkanKHR*            graphicsRequirements);

// Module metadata

const char* vulkan_function_names[] {
    "xrGetVulkanInstanceExtensionsKHR",
    "xrGetVulkanDeviceExtensionsKHR",
    "xrGetVulkanGraphicsDeviceKHR",
    "xrGetVulkanGraphicsRequirementsKHR"
};
const char* vulkan2_function_names[] {
    "xrCreateVulkanInstanceKHR",
    "xrCreateVulkanDeviceKHR",
    "xrGetVulkanGraphicsDevice2KHR",
    "xrGetVulkanGraphicsRequirements2KHR"
};

ModuleExtension extensions[] {
    {
        .extension_name = XR_KHR_VULKAN_ENABLE_EXTENSION_NAME,
        .extension_version = 10,
        .num_functions = sizeof(vulkan_function_names) / sizeof(const char*),
        .function_names = vulkan_function_names
    },
    {
        .extension_name = XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
        .extension_version = 2,
        .num_functions = sizeof(vulkan2_function_names) / sizeof(const char*),
        .function_names = vulkan2_function_names
    }
};

ModuleLayerFunction functions[] {
    {
        .function_name = "xrGetVulkanInstanceExtensionsKHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanInstanceExtensionsKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanInstanceExtensionsKHR_next
    },
    {
        .function_name = "xrGetVulkanDeviceExtensionsKHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanDeviceExtensionsKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanDeviceExtensionsKHR_next
    },
    {
        .function_name = "xrGetVulkanGraphicsDeviceKHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanGraphicsDeviceKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanGraphicsDeviceKHR_next
    },
    {
        .function_name = "xrGetVulkanGraphicsRequirementsKHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanGraphicsRequirementsKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanGraphicsRequirementsKHR_next
    },
    {
        .function_name = "xrCreateVulkanInstanceKHR",
        .new_function = (PFN_xrVoidFunction)xrCreateVulkanInstanceKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrCreateVulkanInstanceKHR_next
    },
    {
        .function_name = "xrCreateVulkanDeviceKHR",
        .new_function = (PFN_xrVoidFunction)xrCreateVulkanDeviceKHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrCreateVulkanDeviceKHR_next
    },
    {
        .function_name = "xrGetVulkanGraphicsDevice2KHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanGraphicsDevice2KHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanGraphicsDevice2KHR_next
    },
    {
        .function_name = "xrGetVulkanGraphicsRequirements2KHR",
        .new_function = (PFN_xrVoidFunction)xrGetVulkanGraphicsRequirements2KHRImpl,
        .old_function = (PFN_xrVoidFunction*)&pfn_xrGetVulkanGraphicsRequirements2KHR_next
    },
    {
        .function_name = "xrCreateSwapchain",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrCreateSwapchainImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrCreateSwapchain_next
    },
    {
        .function_name = "xrDestroySwapchain",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrDestroySwapchainImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrDestroySwapchain_next
    },
    {
        .function_name = "xrEnumerateSwapchainImages",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrEnumerateSwapchainImagesImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrEnumerateSwapchainImages_next
    },
    {
        .function_name = "xrAcquireSwapchainImage",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrAcquireSwapchainImageImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrAcquireSwapchainImage_next
    },
    {
        .function_name = "xrWaitSwapchainImage",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrWaitSwapchainImageImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrWaitSwapchainImage_next
    },
    {
        .function_name = "xrReleaseSwapchainImage",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrReleaseSwapchainImageImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrReleaseSwapchainImage_next
    },
    {
        .function_name = "xrCreateSession",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrCreateSessionImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrCreateSession_next
    },
    {
        .function_name = "xrDestroySession",
        .new_function = (PFN_xrVoidFunction)vulkan_core::xrDestroySessionImpl,
        .old_function = (PFN_xrVoidFunction*)&vulkan_core::pfn_xrDestroySession_next
    }
};

ModuleInfo module_info {
    .num_extensions = sizeof(extensions) / sizeof(ModuleExtension),
    .extensions = extensions,
    .num_functions = sizeof(functions) / sizeof(ModuleLayerFunction),
    .functions = functions,
};

// Static data

std::unique_ptr<Transport> transport;
XrInstance saved_xr_instance = XR_NULL_HANDLE;
VkInstance saved_vk_instance = VK_NULL_HANDLE;
PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr;

// Module interface
class VulkanClientModule : public ClientModule {
public:
    explicit VulkanClientModule(xrtp_Transport transport_handle) {
        transport = std::make_unique<Transport>(transport_handle);
        vulkan_core::set_transport(transport_handle);
    }

    const ModuleInfo* get_module_info() override {
        return &module_info;
    }

    void on_instance(XrInstance instance, PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr) override {
        vulkan_core::set_xr_instance(instance);
        saved_xr_instance = instance;
    }
};

// Function implementations

XrResult return_string_buffer(
    const char* buffer,
    size_t buffer_size,
    uint32_t capacity_in,
    uint32_t* capacity_out,
    char* buffer_out
) {
    uint32_t buffer_size_int = static_cast<uint32_t>(buffer_size);

    if (capacity_out == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    *capacity_out = buffer_size_int;

    if (capacity_in == 0) {
        return XR_SUCCESS;
    }

    if (capacity_in < buffer_size_int) {
        return XR_ERROR_SIZE_INSUFFICIENT;
    }

    if (buffer_out == nullptr) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    std::memcpy(buffer_out, buffer, buffer_size);
    return XR_SUCCESS;
}

XrResult xrGetVulkanInstanceExtensionsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer)
{
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    // initialize the function loader with linked vkGetInstanceProcAddr
    // this is as good a place as any to do it, because if this function is called, we know
    // that the application is using VK_KHR_vulkan_enable.
    // TODO: have the instance callback tell modules which extensions were enabled so that
    // modules that support multiple extensions can know which one(s) are being used.
    vulkan_core::initialize_vulkan(vkGetInstanceProcAddr);

    // shouldn't need to specify this because it's included in Vulkan 1.1 but applications
    // can ignore the runtime's request for Vulkan 1.1 (looking at you HelloXR)
    const char extensions[] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;

    return return_string_buffer(
        extensions,
        sizeof(extensions),
        bufferCapacityInput,
        bufferCountOutput,
        buffer
    );
}

XrResult xrGetVulkanDeviceExtensionsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    uint32_t                                    bufferCapacityInput,
    uint32_t*                                   bufferCountOutput,
    char*                                       buffer)
{
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    const char extensions[] =
#ifdef _WIN32
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME " "
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME " "
#else
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME " "
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME " "
#endif
        // need to specify these even though they're in Vulkan 1.1, because applications might
        // ignore our requirement of Vulkan 1.1 :(
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME " "
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME " "
        VK_KHR_MULTIVIEW_EXTENSION_NAME;

    return return_string_buffer(
        extensions,
        sizeof(extensions),
        bufferCapacityInput,
        bufferCountOutput,
        buffer
    );
}

VkPhysicalDevice get_physical_device(VkInstance vk_instance, PFN_vkGetInstanceProcAddr pfn_vkGetInstanceProcAddr)  {
    auto msg_out = transport->start_message(XRTP_MSG_VULKAN2_GET_PHYSICAL_DEVICE);
    msg_out.flush();

    auto msg_in = transport->await_message(XRTP_MSG_VULKAN2_RETURN_PHYSICAL_DEVICE);
    DeserializeContext d_ctx(msg_in.buffer);
    uint8_t target_uuid[VK_UUID_SIZE]{};
    deserialize_array(target_uuid, VK_UUID_SIZE, d_ctx);

    auto pfn_vkEnumeratePhysicalDevices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(pfn_vkGetInstanceProcAddr(
            vk_instance,
            "vkEnumeratePhysicalDevices"
        ));
    auto pfn_vkGetPhysicalDeviceProperties2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(pfn_vkGetInstanceProcAddr(
            vk_instance,
            "vkGetPhysicalDeviceProperties2"
        ));

    VkResult result{};

    uint32_t device_count{};
    result = pfn_vkEnumeratePhysicalDevices(vk_instance, &device_count, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Unable to get physical device count: " + std::to_string(result));
    }

    std::vector<VkPhysicalDevice> devices(device_count);
    result = pfn_vkEnumeratePhysicalDevices(vk_instance, &device_count, devices.data());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Unable to list physical devices: " + std::to_string(result));
    }

    VkPhysicalDevice found_device = VK_NULL_HANDLE;

    for (VkPhysicalDevice phys : devices) {
        VkPhysicalDeviceIDProperties id_props{};
        id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &id_props;

        pfn_vkGetPhysicalDeviceProperties2(phys, &props2);

        if (std::memcmp(id_props.deviceUUID, target_uuid, VK_UUID_SIZE) == 0) {
            found_device = phys;
            break;
        }
    }

    if (!found_device) {
        throw std::runtime_error("Did not find any devices with UUID supplied by server");
    }

    return found_device;
}

XrResult xrGetVulkanGraphicsDeviceKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    VkInstance                                  vkInstance,
    VkPhysicalDevice*                           vkPhysicalDevice)
try {
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    // for this extension, use the linked vkGetInstanceProcAddr
    *vkPhysicalDevice = get_physical_device(vkInstance, vkGetInstanceProcAddr);

    return XR_SUCCESS;
}
catch (const std::exception& e) {
    spdlog::error("Exception thrown in xrGetVulkanGraphicsDeviceKHRImpl: {}", e.what());
    return XR_ERROR_RUNTIME_FAILURE;
}

XrResult xrGetVulkanGraphicsRequirementsKHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrGraphicsRequirementsVulkanKHR*            graphicsRequirements)
{
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    vulkan_core::on_graphics_requirements_called();
    graphicsRequirements->minApiVersionSupported = XR_MAKE_VERSION(1, 1, 0);
    graphicsRequirements->maxApiVersionSupported = XR_MAKE_VERSION(1, 4, 0);

    return XR_SUCCESS;
}

void add_string_if_not_present(std::vector<const char*>& strings, const char* string) {
    // return if string is present
    for (const char* e : strings) {
        if (std::strncmp(e, string, VK_MAX_EXTENSION_NAME_SIZE) == 0) {
            return;
        }
    }
    strings.push_back(string);
}

XrResult xrCreateVulkanInstanceKHRImpl(
    XrInstance                                  instance,
    const XrVulkanInstanceCreateInfoKHR*        createInfo,
    VkInstance*                                 vulkanInstance,
    VkResult*                                   vulkanResult)
{
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    pfn_vkGetInstanceProcAddr = createInfo->pfnGetInstanceProcAddr;
    vulkan_core::initialize_vulkan(pfn_vkGetInstanceProcAddr);

    auto pfn_vkCreateInstance = 
        reinterpret_cast<PFN_vkCreateInstance>(pfn_vkGetInstanceProcAddr(
            VK_NULL_HANDLE,
            "vkCreateInstance"
        ));
    auto pfn_vkDestroyInstance =
        reinterpret_cast<PFN_vkDestroyInstance>(pfn_vkGetInstanceProcAddr(
            VK_NULL_HANDLE,
            "vkDestroyInstance"
        ));

    if (saved_vk_instance) {
        vkDestroyInstance(saved_vk_instance, nullptr);
        saved_vk_instance = VK_NULL_HANDLE;
    }

    // copy create info so that it can be edited
    VkInstanceCreateInfo vulkan_create_info{};
    std::memcpy(&vulkan_create_info, createInfo->vulkanCreateInfo, sizeof(VkInstanceCreateInfo));

    // copy application info so that it can be edited
    VkApplicationInfo vulkan_application_info{};
    std::memcpy(&vulkan_application_info, vulkan_create_info.pApplicationInfo, sizeof(VkApplicationInfo));
    vulkan_create_info.pApplicationInfo = &vulkan_application_info;

    // update requested API version
    vulkan_application_info.apiVersion = std::max(vulkan_application_info.apiVersion, VK_API_VERSION_1_1);

    std::vector<const char*> requested_extensions(
        vulkan_create_info.ppEnabledExtensionNames,
        vulkan_create_info.ppEnabledExtensionNames + vulkan_create_info.enabledExtensionCount
    );
    // For future use:
    // add_string_if_not_present(requested_extensions, "VK_KHR_example_instance_extension");
    vulkan_create_info.enabledExtensionCount = requested_extensions.size();
    vulkan_create_info.ppEnabledExtensionNames = requested_extensions.data();

    std::vector<const char*> requested_layers(
        vulkan_create_info.ppEnabledLayerNames,
        vulkan_create_info.ppEnabledLayerNames + vulkan_create_info.enabledLayerCount
    );
#ifndef NDEBUG
    add_string_if_not_present(requested_layers, "VK_LAYER_KHRONOS_validation");
#endif
    vulkan_create_info.enabledLayerCount = requested_layers.size();
    vulkan_create_info.ppEnabledLayerNames = requested_layers.data();

    *vulkanResult = pfn_vkCreateInstance(&vulkan_create_info, createInfo->vulkanAllocator, vulkanInstance);
    if (*vulkanResult != VK_SUCCESS) {
        return XR_ERROR_VALIDATION_FAILURE; // not obvious from the spec which error I should return here
    }

    saved_vk_instance = *vulkanInstance;

    return XR_SUCCESS;
}

XrResult xrCreateVulkanDeviceKHRImpl(
    XrInstance                                  instance,
    const XrVulkanDeviceCreateInfoKHR*          createInfo,
    VkDevice*                                   vulkanDevice,
    VkResult*                                   vulkanResult)
{
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    assert(pfn_vkGetInstanceProcAddr == createInfo->pfnGetInstanceProcAddr);

    // it's odd that this function doesn't get passed a VkInstance, or at least it's not expliclty
    // stated that a runtime should hold onto its VkInstance handle for later use.

    VkDeviceCreateInfo vulkan_create_info{};
    std::memcpy(&vulkan_create_info, createInfo->vulkanCreateInfo, sizeof(VkDeviceCreateInfo));

    std::vector<const char*> requested_extensions(
        vulkan_create_info.ppEnabledExtensionNames,
        vulkan_create_info.ppEnabledExtensionNames + vulkan_create_info.enabledExtensionCount
    );
#ifdef _WIN32
    add_string_if_not_present(requested_extensions, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
    add_string_if_not_present(requested_extensions, VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#else
    add_string_if_not_present(requested_extensions, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    add_string_if_not_present(requested_extensions, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
#endif
    vulkan_create_info.enabledExtensionCount = requested_extensions.size();
    vulkan_create_info.ppEnabledExtensionNames = requested_extensions.data();

    auto pfn_vkCreateDevice =
        reinterpret_cast<PFN_vkCreateDevice>(createInfo->pfnGetInstanceProcAddr(
            saved_vk_instance,
            "vkCreateDevice"
        ));

    *vulkanResult = pfn_vkCreateDevice(createInfo->vulkanPhysicalDevice, &vulkan_create_info, createInfo->vulkanAllocator, vulkanDevice);
    if (*vulkanResult != VK_SUCCESS) {
        return XR_ERROR_VALIDATION_FAILURE;
    }

    return XR_SUCCESS;
}

XrResult xrGetVulkanGraphicsDevice2KHRImpl(
    XrInstance                                  instance,
    const XrVulkanGraphicsDeviceGetInfoKHR*     getInfo,
    VkPhysicalDevice*                           vulkanPhysicalDevice)
try {
    if (instance != saved_xr_instance) {
        return XR_ERROR_HANDLE_INVALID;
    }

    // use pfn_vkGetInstanceProcAddr saved from xrCreateVulkanInstanceKHR
    *vulkanPhysicalDevice = get_physical_device(getInfo->vulkanInstance, pfn_vkGetInstanceProcAddr);

    return XR_SUCCESS;
}
catch (const std::exception& e) {
    spdlog::error("Exception thrown in xrGetVulkanGraphicsDevice2KHRImpl: {}", e.what());
    return XR_ERROR_RUNTIME_FAILURE;
}

XrResult xrGetVulkanGraphicsRequirements2KHRImpl(
    XrInstance                                  instance,
    XrSystemId                                  systemId,
    XrGraphicsRequirementsVulkanKHR*            graphicsRequirements)
{
    // these two functions are identical
    return xrGetVulkanGraphicsRequirementsKHRImpl(instance, systemId, graphicsRequirements);
}

} // namespace vulkan

namespace xrtransport {

std::unique_ptr<ClientModule> VulkanClientModuleFactory::create(xrtp_Transport transport_handle) {
    return std::make_unique<vulkan::VulkanClientModule>(transport_handle);
}

} // namespace xrtransport

#ifdef XRTRANSPORT_DYNAMIC_MODULES

// Dynamic module entry point
XRTP_API_EXPORT ClientModule* xrtp_get_client_module(
    xrtp_Transport transport_handle)
{
    return VulkanClientModuleFactory::create(transport_handle).release();
}

#endif