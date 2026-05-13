// SPDX-License-Identifier: LGPL-3.0-or-later

#include "available_extensions.h"

#include "xrtransport/extensions/enabled_extensions.h"
#include "xrtransport/extensions/extension_functions.h"

#include "rpc.h"

#include <openxr/openxr.h>
#include <spdlog/spdlog.h>

#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

using std::uint32_t;

namespace xrtransport {

static std::unordered_map<std::string, ExtensionInfo> collect_rpc_available_extensions() {
    std::unordered_map<std::string, ExtensionInfo> rpc_available_extensions;

    XrResult result;
    uint32_t extension_count{};
    result = rpc::xrEnumerateInstanceExtensionProperties(nullptr, 0, &extension_count, nullptr);
    if (!XR_SUCCEEDED(result)) {
        spdlog::error("Failed to enumerate server extensions: {}", (int)result);
        return rpc_available_extensions; // empty
    }
    std::vector<XrExtensionProperties> extension_properties_vector(extension_count, {XR_TYPE_EXTENSION_PROPERTIES});
    result = rpc::xrEnumerateInstanceExtensionProperties(nullptr, extension_count, &extension_count, extension_properties_vector.data());
    if (!XR_SUCCEEDED(result)) {
        spdlog::error("Failed to enumerate server extensions: {}", (int)result);
        return rpc_available_extensions; // empty
    }

    for (auto& extension_properties : extension_properties_vector) {
        std::string extension_name(extension_properties.extensionName);

        // available extensions is the intersection of enabled (compiled) extensions and the extensions the server runtime reports
        if (enabled_extensions.find(extension_name) == enabled_extensions.end()) continue;

        uint32_t transport_version = enabled_extensions.at(extension_name);
        uint32_t server_version = extension_properties.extensionVersion;

        // OpenXR extensions are backwards-compatible, so choose the lowest common version
        uint32_t available_version = std::min(transport_version, server_version);

        ExtensionInfo ext_info{available_version, extension_functions.at(extension_name)};

        rpc_available_extensions.emplace(extension_name, std::move(ext_info));
    }

    // extensions built into the runtime
#ifdef _WIN32
    rpc_available_extensions.emplace("XR_KHR_win32_convert_performance_counter_time", ExtensionInfo{
        1,
        {
            "xrConvertWin32PerformanceCounterToTimeKHR",
            "xrConvertTimeToWin32PerformanceCounterKHR",
        }
    });
#else
    ExtensionInfo asdf;
    rpc_available_extensions.emplace("XR_KHR_convert_timespec_time", ExtensionInfo{
        1,
        {
            "xrConvertTimespecTimeToTimeKHR",
            "xrConvertTimeToTimespecTimeKHR",
        }
    });
#endif
#ifdef __ANDROID__
    rpc_available_extensions.emplace("XR_KHR_android_create_instance", ExtensionInfo{3, {}});
#endif

    return rpc_available_extensions;
}

std::unordered_map<std::string, ExtensionInfo> collect_available_extensions(const std::vector<LoadedModule>& modules) {
    // gather (runtime) available extensions and add extensions provided by modules.
    // module extensions override ones provided by the runtime. If multiple modules define the same
    // extension, the last one will overwrite the others, but this should be fine if they all define the
    // same functions.
    auto result = collect_rpc_available_extensions();
    for (const auto& module : modules) {
        auto& module_info = *module.module->get_module_info();
        for (uint32_t i = 0; i < module_info.num_extensions; i++) {
            const ModuleExtension& extension = module_info.extensions[i];
            std::vector<std::string> extension_functions;
            for (uint32_t j = 0; j < extension.num_functions; j++) {
                extension_functions.emplace_back(extension.function_names[j]);
            }
            result[extension.extension_name] = {extension.extension_version, extension_functions};
        }
    }

    return result;
}

} // namespace xrtransport