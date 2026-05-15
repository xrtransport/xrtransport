#ifndef XRTRANSPORT_VULKAN_LOADER_H
#define XRTRANSPORT_VULKAN_LOADER_H

#include <vulkan/vulkan.h>

namespace xrtransport {

// list of functions that can be gotten from vkGetInstanceProcAddr with NULL
#define XRTP_VULKAN_GLOBAL_FUNCTIONS(_) \
    _(EnumerateInstanceVersion) \
    _(EnumerateInstanceExtensionProperties) \
    _(EnumerateInstanceLayerProperties) \
    _(CreateInstance)

// list of functions that can be gotten from vkGetInstanceProcAddr with a valid instance
#define XRTP_VULKAN_INSTANCE_FUNCTIONS(_) \
    _(GetDeviceProcAddr) \
    _(EnumeratePhysicalDevices) \
    _(GetPhysicalDeviceQueueFamilyProperties) \
    _(GetPhysicalDeviceProperties2) \
    _(GetPhysicalDeviceMemoryProperties) \
    _(CreateDevice)

// list of functions that can be gotten from vkGetDeviceProcAddr with a valid instance
#define XRTP_VULKAN_DEVICE_FUNCTIONS(_) \
    _(GetMemoryFdKHR) \
    _(GetSemaphoreFdKHR) \
    _(ImportSemaphoreFdKHR) \
    _(GetDeviceQueue) \
    _(CreateCommandPool) \
    _(CreateSemaphore) \
    _(AllocateCommandBuffers) \
    _(CreateFence) \
    _(CreateImage) \
    _(GetImageMemoryRequirements) \
    _(AllocateMemory) \
    _(BindImageMemory) \
    _(QueueWaitIdle) \
    _(DestroyImage) \
    _(FreeMemory) \
    _(DestroySemaphore) \
    _(WaitForFences) \
    _(ResetFences) \
    _(BeginCommandBuffer) \
    _(CmdPipelineBarrier) \
    _(CmdCopyImage) \
    _(EndCommandBuffer) \
    _(QueueSubmit) \
    _(DeviceWaitIdle) \
    _(DestroyCommandPool) \
    _(DestroyFence) \
    _(FreeCommandBuffers)


// declare the function as a class member
#define XRTP_DECLARE_FUNCTION(name) \
    const PFN_vk##name name = nullptr;

// declare the function as a function parameter
// note: each item has a comma before it so the parameter list must start with something
#define XRTP_DECLARE_PARAMETER(name) \
    , PFN_vk##name name

// initialize the member function with a parameter of the same name
#define XRTP_INITIALIZER(name) \
    , name(name)

// only the name of the function with a preceding comma, to be used when calling constructors
#define XRTP_PARAMETER(name) \
    , name

#define XRTP_LOAD_FUNCTION(name, load_func, load_arg) \
    auto name = reinterpret_cast<PFN_vk##name>(load_func(load_arg, "vk" #name));

#define XRTP_LOAD_GLOBAL_FUNCTION(name) \
    XRTP_LOAD_FUNCTION(name, GetInstanceProcAddr, NULL)

#define XRTP_LOAD_INSTANCE_FUNCTION(name) \
    XRTP_LOAD_FUNCTION(name, GetInstanceProcAddr, instance)

#define XRTP_LOAD_DEVICE_FUNCTION(name) \
    XRTP_LOAD_FUNCTION(name, GetDeviceProcAddr, device)


/**
 * Immutable Vulkan loader class.
 * 
 * Contains a public const member for every Vulkan function used.
 * To add more, add to the XRTP_VULKAN_*_FUNCTIONS lists defined above.
 * 
 * Each loader stage (global, instance, device) returns a new instance which cannot be modified.
 */
class VulkanLoader {
public:
    const PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;

    // global-level functions
    XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_DECLARE_FUNCTION)

    // instance-level functions
    XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_DECLARE_FUNCTION)

    // device-level
    XRTP_VULKAN_DEVICE_FUNCTIONS(XRTP_DECLARE_FUNCTION)

private:
    // global-level constructor
    VulkanLoader(
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr
        XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_DECLARE_PARAMETER)
    )
    : GetInstanceProcAddr(GetInstanceProcAddr)
    XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_INITIALIZER)
    {}

    // instance-level constructor
    VulkanLoader(
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr
        XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_DECLARE_PARAMETER)
        XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_DECLARE_PARAMETER)
    )
    : GetInstanceProcAddr(GetInstanceProcAddr)
    XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_INITIALIZER)
    XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_INITIALIZER)
    {}

    // device-level constructor
    VulkanLoader(
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr
        XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_DECLARE_PARAMETER)
        XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_DECLARE_PARAMETER)
        XRTP_VULKAN_DEVICE_FUNCTIONS(XRTP_DECLARE_PARAMETER)
    )
    : GetInstanceProcAddr(GetInstanceProcAddr)
    XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_INITIALIZER)
    XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_INITIALIZER)
    XRTP_VULKAN_DEVICE_FUNCTIONS(XRTP_INITIALIZER)
    {}

public:
    static VulkanLoader init_global(PFN_vkGetInstanceProcAddr GetInstanceProcAddr) {
        // load global functions into local variables
        XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_LOAD_GLOBAL_FUNCTION);

        return VulkanLoader(
            GetInstanceProcAddr
            XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_PARAMETER) // local variables
        );
    }

    VulkanLoader init_instance(VkInstance instance) const {
        // locally loaded instance functions shadow members
        XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_LOAD_INSTANCE_FUNCTION)

        return VulkanLoader(
            GetInstanceProcAddr
            XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_PARAMETER) // existing member functions
            XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_PARAMETER) // local variables
        );
    }

    VulkanLoader init_device(VkDevice device) const {
        // locally loaded device functions shadow members
        XRTP_VULKAN_DEVICE_FUNCTIONS(XRTP_LOAD_DEVICE_FUNCTION)

        return VulkanLoader(
            GetInstanceProcAddr
            XRTP_VULKAN_GLOBAL_FUNCTIONS(XRTP_PARAMETER) // existing member functions
            XRTP_VULKAN_INSTANCE_FUNCTIONS(XRTP_PARAMETER) // existing member functions
            XRTP_VULKAN_DEVICE_FUNCTIONS(XRTP_PARAMETER) // local variables
        );
    }
};

// undef utility macros, but keep list macros in case they are useful
#undef XRTP_DECLARE_FUNCTION
#undef XRTP_DECLARE_PARAMETER
#undef XRTP_INITIALIZER
#undef XRTP_PARAMETER
#undef XRTP_LOAD_FUNCTION
#undef XRTP_LOAD_GLOBAL_FUNCTION
#undef XRTP_LOAD_INSTANCE_FUNCTION
#undef XRTP_LOAD_DEVICE_FUNCTION

} // namespace xrtransport

#endif // XRTRANSPORT_VULKAN_LOADER_H