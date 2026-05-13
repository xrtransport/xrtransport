// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_HANDLE_EXCHANGE_FACTORY_H
#define XRTRANSPORT_SERVER_HANDLE_EXCHANGE_FACTORY_H

#include "xrtransport/transport/transport.h"
#include "xrtransport/server/module_interface.h"

#include <memory>

namespace xrtransport {

class HandleExchangeServerModuleFactory {
public:
    static std::unique_ptr<ServerModule> create(
        xrtp_Transport transport,
        FunctionLoader* function_loader,
        uint32_t num_extensions,
        const XrExtensionProperties* extensions);
};

} // namespace xrtransport

#endif // XRTRANSPORT_SERVER_HANDLE_EXCHANGE_FACTORY_H
