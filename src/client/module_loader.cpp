// SPDX-License-Identifier: LGPL-3.0-or-later

#include "module_loader.h"
#ifndef XRTRANSPORT_DYNAMIC_MODULES
// include header for static modules
#include <static_client_modules.h>
#endif

#include "xrtransport/transport/transport_c_api.h"
#include "xrtransport/client/module_signature.h"

#include <spdlog/spdlog.h>

// dll loading
#if defined(__linux__)
#include <dlfcn.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#error Windows, Linux, or Android required
#endif

#include <filesystem>
#include <optional>
#include <cassert>
#include <cstring>
#include <stdexcept>

namespace fs = std::filesystem;

namespace xrtransport {

using PFN_get_client_module = decltype(&xrtp_get_client_module);

static std::optional<fs::path> get_runtime_folder() {
#if defined(_WIN32)
    HMODULE handle = nullptr;
    if (GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCTSTR)get_runtime_folder,
        &handle
    )) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(handle, path, MAX_PATH)) {
            return fs::path(std::string(path)).parent_path();
        }
    }
    return std::nullopt;
#elif defined(__linux__)
    Dl_info info;
    if (dladdr((void*)get_runtime_folder, &info)) {
        return fs::path(std::string(info.dli_fname)).parent_path();
    }
    return std::nullopt;
#endif
}

static inline bool is_filename_module(std::string_view filename) {
#if defined(_WIN32)
    constexpr std::string_view module_prefix = "module_";
    constexpr std::string_view module_ext = ".dll";
#elif defined(__linux__)
    constexpr std::string_view module_prefix = "libmodule_";
    constexpr std::string_view module_ext = ".so";
#endif
    bool prefix_matches = filename.size() >= module_prefix.size()
        && std::equal(module_prefix.begin(), module_prefix.end(), filename.begin());
    bool ext_matches = filename.size() >= module_ext.size()
        && std::equal(module_ext.rbegin(), module_ext.rend(), filename.rbegin());
    
    return prefix_matches && ext_matches;
}

static PFN_get_client_module load_dynamic_module(fs::path module_path) {
    // Note: this function intentionally doesn't provide any way to unload the modules.
    // they should be loaded for the duration of the program.

    spdlog::info("Loading module at {}", module_path.string());

#if defined(_WIN32)
    #error TODO: implement
#elif defined(__linux__)
    void* module = dlopen(module_path.c_str(), RTLD_LAZY);
    auto result = reinterpret_cast<PFN_get_client_module>(dlsym(module, "xrtp_get_client_module"));
    return result;
#endif
}

static bool modules_loaded = false;

static std::vector<LoadedModule> load_dynamic_modules(xrtp_Transport transport) {
    if (modules_loaded) {
        throw std::runtime_error("Attempted to load modules twice");
    }

    auto opt_runtime_folder = get_runtime_folder();
    if (!opt_runtime_folder.has_value()) {
        spdlog::warn("Unable to detect runtime folder, not loading any modules");
        return {};
    }

    fs::path runtime_folder = opt_runtime_folder.value();
    assert(fs::exists(runtime_folder) && fs::is_directory(runtime_folder));

    std::vector<LoadedModule> result;

    for (const auto& entry : fs::directory_iterator(runtime_folder)) {
        if (!entry.is_regular_file()) continue;
        if (!is_filename_module(entry.path().filename().string())) continue;

        auto pfn_get_client_module = load_dynamic_module(entry.path());

        auto p_client_module = std::unique_ptr<ClientModule>(pfn_get_client_module(transport));
        if (!p_client_module) {
            spdlog::error("Module at {} returned null ClientModule", entry.path().c_str());
        }
        result.emplace_back(LoadedModule{std::move(p_client_module)});
    }

    modules_loaded = true;
    return result;
}

#ifndef XRTRANSPORT_DYNAMIC_MODULES
static std::vector<LoadedModule> load_static_modules(xrtp_Transport transport) {
    std::vector<std::unique_ptr<ClientModule>> raw_modules = get_static_client_modules(transport);

    std::vector<LoadedModule> result;
    for (auto& raw_module : raw_modules) {
        result.emplace_back(LoadedModule{std::move(raw_module)});
    }

    return result;
}
#endif

std::vector<LoadedModule> load_modules(xrtp_Transport transport) {
#ifdef XRTRANSPORT_DYNAMIC_MODULES
    return load_dynamic_modules(transport);
#else
    return load_static_modules(transport);
#endif
}

} // namespace xrtransport