// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_AVAILABLE_EXTENSIONS_H
#define XRTRANSPORT_AVAILABLE_EXTENSIONS_H

#include "module_loader.h"

#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>

namespace xrtransport {

struct ExtensionInfo {
    std::uint32_t version;
    std::vector<std::string> function_names;
};

std::unordered_map<std::string, ExtensionInfo> collect_available_extensions(const std::vector<LoadedModule>& modules_info);

}

#endif // XRTRANSPORT_AVAILABLE_EXTENSIONS_H