// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_VULKAN_CLIENT_MODULE_FACTORY_H
#define XRTRANSPORT_VULKAN_CLIENT_MODULE_FACTORY_H

#include "xrtransport/client/module_interface.h"

#include <memory>

namespace xrtransport {

class VulkanClientModuleFactory {
public:
    static std::unique_ptr<ClientModule> create(xrtp_Transport transport_handle);
};

} // namespace xrtransport

#endif // XRTRANSPORT_VULKAN_CLIENT_MODULE_FACTORY_H