// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_STATIC_CLIENT_MODULES_H
#define XRTRANSPORT_STATIC_CLIENT_MODULES_H

#include "xrtransport/client/module_interface.h"

#include <memory>
#include <vector>

namespace xrtransport {

std::vector<std::unique_ptr<ClientModule>> get_static_client_modules(xrtp_Transport transport);

} // namespace xrtransport

#endif //XRTRANSPORT_STATIC_CLIENT_MODULES_H
