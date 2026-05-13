// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_VULKAN_SERVER_MODULE_FACTORY_H
#define XRTRANSPORT_VULKAN_SERVER_MODULE_FACTORY_H

#include "xrtransport/server/module_interface.h"

#include <memory>

namespace xrtransport {

class VulkanServerModuleFactory {
public:
    static std::unique_ptr<ServerModule> create(
        xrtp_Transport transport,
        FunctionLoader* function_loader,
        std::uint32_t num_extensions,
        const XrExtensionProperties* extensions);
};

} // namespace xrtransport

#endif // XRTRANSPORT_VULKAN_SERVER_MODULE_FACTORY_H