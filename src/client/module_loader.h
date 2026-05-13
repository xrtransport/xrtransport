// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_CLIENT_MODULE_LOADER_H
#define XRTRANSPORT_CLIENT_MODULE_LOADER_H

#include <memory>

#include "xrtransport/client/module_interface.h"
#include "xrtransport/transport/transport_c_api.h"

#include "function_table.h"

#include <openxr/openxr.h>

#include <vector>

namespace xrtransport {

struct LoadedModule {
    // no need to keep library handle because library stays loaded
    std::unique_ptr<ClientModule> module;
};

std::vector<LoadedModule> load_modules(xrtp_Transport transport);

} // namespace xrtransport

#endif // XRTRANSPORT_CLIENT_MODULE_LOADER_H