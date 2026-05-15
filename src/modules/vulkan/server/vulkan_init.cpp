#include "vulkan_init.h"

#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
#include "vulkan_dispatch.h"
using namespace gfxstream::host::vk;
#endif

namespace xrtransport {

PFN_vkGetInstanceProcAddr get_vulkan_init_function() {
#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
    // get the function from the static VulkanDispatch
    return vkDispatch()->vkGetInstanceProcAddr;
#else
    // get the function from the linked Vulkan loader
    return vkGetInstanceProcAddr;
#endif
}

} // namespace xrtransport