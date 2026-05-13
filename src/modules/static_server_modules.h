// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_STATIC_CLIENT_MODULES_H
#define XRTRANSPORT_STATIC_CLIENT_MODULES_H

#include "xrtransport/server/module_interface.h"
#include "xrtransport/transport/transport.h"
#include "xrtransport/server/function_loader.h"

#include <openxr/openxr.h>

#include <memory>
#include <vector>

namespace xrtransport {

std::vector<std::unique_ptr<ServerModule>> get_static_server_modules(
    xrtp_Transport transport,
    FunctionLoader* function_loader,
    uint32_t num_extensions,
    const XrExtensionProperties* extensions
);

} // namespace xrtransport

#endif //XRTRANSPORT_STATIC_CLIENT_MODULES_H
