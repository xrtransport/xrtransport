// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_CLIENT_HANDLE_EXCHANGE_FACTORY_H
#define XRTRANSPORT_CLIENT_HANDLE_EXCHANGE_FACTORY_H

#include "xrtransport/transport/transport.h"
#include "xrtransport/client/module_interface.h"

#include <memory>

namespace xrtransport {

class HandleExchangeClientModuleFactory {
public:
    static std::unique_ptr<ClientModule> create(xrtp_Transport transport);
};

} // namespace xrtransport

#endif // XRTRANSPORT_CLIENT_HANDLE_EXCHANGE_FACTORY_H
