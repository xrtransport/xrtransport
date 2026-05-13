// SPDX-License-Identifier: LGPL-3.0-or-later

#include "static_client_modules.h"

#include "client_handle_exchange_factory.h"
#include "vulkan_client_module_factory.h"

namespace xrtransport {

std::vector<std::unique_ptr<ClientModule>> get_static_client_modules(xrtp_Transport transport) {
    std::vector<std::unique_ptr<ClientModule>> modules;
#ifdef XRTRANSPORT_BUILD_MODULE_HANDLE_EXCHANGE
    modules.emplace_back(HandleExchangeClientModuleFactory::create(transport));
#endif
#ifdef XRTRANSPORT_BUILD_MODULE_VULKAN
    modules.emplace_back(VulkanClientModuleFactory::create(transport));
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