#ifndef XRTRANSPORT_VULKAN_SERVER_VULKAN_INIT_H
#define XRTRANSPORT_VULKAN_SERVER_VULKAN_INIT_H

#include <vulkan/vulkan.h>

namespace xrtransport {

/**
 * This interface is to hide the implementation detail of whether we're getting Vulkan pointers from the
 * linked Vulkan loader, or from gfxstream
 */
PFN_vkGetInstanceProcAddr get_vulkan_init_function();

} // namespace xrtransport

#endif