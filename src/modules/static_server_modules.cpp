// SPDX-License-Identifier: LGPL-3.0-or-later

#include "static_server_modules.h"

#include "server_handle_exchange_factory.h"
#include "vulkan_server_module_factory.h"

namespace xrtransport {

std::vector<std::unique_ptr<ServerModule>> get_static_server_modules(
    xrtp_Transport transport,
    FunctionLoader* function_loader,
    uint32_t num_extensions,
    const XrExtensionProperties* extensions
) {
    std::vector<std::unique_ptr<ServerModule>> modules;
#ifdef XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE
    modules.emplace_back(HandleExchangeServerModuleFactory::create(transport, function_loader, num_extensions, extensions));
#endif
#ifdef XRTRANSPORT_BUILD_MODULE_VULKAN
    modules.emplace_back(VulkanServerModuleFactory::create(transport, function_loader, num_extensions, extensions));
#endif

    // remove any modules that failed to enable
    for (auto it = modules.begin(); it != modules.end(); ) {
        if (*it == nullptr) {
            it = modules.erase(it);
        }
        else {
            ++it;
        }
    }

    return modules;
}

} // namespace xrtransport