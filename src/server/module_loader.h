// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_MODULE_LOADER_H
#define XRTRANSPORT_SERVER_MODULE_LOADER_H

#include "xrtransport/server/function_loader.h"
#include "module.h"

#include <vector>

namespace xrtransport {

class ServerModuleLoader {
public:
    static std::vector<LoadedModule> load_modules(xrtp_Transport transport_handle, FunctionLoader* function_loader);
};

} // namespace xrtransport

#endif // XRTRANSPORT_SERVER_MODULE_LOADER_H