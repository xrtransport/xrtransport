#include "module_loader.h"

#ifndef XRTRANSPORT_DYNAMIC_MODULES
#include "static_server_modules.h"
#endif

#include <filesystem>

// filesystem and module-finding utilities that are not included if using static linking
#ifdef XRTRANSPORT_DYNAMIC_MODULES
namespace fs = std::filesystem;

namespace {

std::string exe_path() {
#ifdef _WIN32
    // Get UTF-16 path
    wchar_t wbuf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, wbuf, MAX_PATH);
    if (len == 0)
        throw std::runtime_error("GetModuleFileNameW failed");

    std::wstring ws(wbuf, len);

    // Convert UTF-16 -> UTF-8
    int size = WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), static_cast<int>(ws.size()),
        nullptr, 0, nullptr, nullptr
    );
    if (size == 0)
        throw std::runtime_error("WideCharToMultiByte sizing failed");

    std::string utf8(size, '\0');
    WideCharToMultiByte(
        CP_UTF8, 0,
        ws.c_str(), static_cast<int>(ws.size()),
        utf8.data(), size, nullptr, nullptr
    );
    return utf8;

#elif __linux__
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len == -1)
        throw std::runtime_error("readlink failed");
    return std::string(buf, len);

#endif
}

bool is_filename_module(std::string_view filename) {
#ifdef _WIN32
    constexpr std::string_view module_prefix = "module_";
    constexpr std::string_view module_ext = ".dll";
#elif __linux__
    constexpr std::string_view module_prefix = "libmodule_";
    constexpr std::string_view module_ext = ".so";
#endif
    bool prefix_matches = filename.size() >= module_prefix.size()
        && std::equal(module_prefix.begin(), module_prefix.end(), filename.begin());
    bool ext_matches = filename.size() >= module_ext.size()
        && std::equal(module_ext.rbegin(), module_ext.rend(), filename.rbegin());

    return prefix_matches && ext_matches;
}

std::vector<std::string> collect_module_paths() {
    fs::path exe_dir = fs::path(exe_path()).parent_path();

    std::vector<std::string> results;

    assert(fs::exists(exe_dir) && fs::is_directory(exe_dir));

    for (const auto& entry : fs::directory_iterator(exe_dir)) {
        if (!entry.is_regular_file()) continue;
        const fs::path& p = entry.path();
        if (!is_filename_module(entry.path().filename().string())) continue;
        results.push_back(p.string());
    }

    return results;
}

} // namespace
#endif

namespace xrtransport {

std::vector<LoadedModule> ServerModuleLoader::load_modules(xrtp_Transport transport_handle, FunctionLoader* function_loader) {
    std::vector<LoadedModule> modules;

    function_loader->ensure_function_loaded("xrEnumerateInstanceExtensionProperties", function_loader->EnumerateInstanceExtensionProperties);
    uint32_t num_extensions{};
    function_loader->EnumerateInstanceExtensionProperties(nullptr, 0, &num_extensions, nullptr);
    std::vector<XrExtensionProperties> extensions(num_extensions, {XR_TYPE_EXTENSION_PROPERTIES});
    function_loader->EnumerateInstanceExtensionProperties(nullptr, num_extensions, &num_extensions, extensions.data());

#ifdef XRTRANSPORT_DYNAMIC_MODULES
    std::vector<std::string> module_paths = collect_module_paths();
    for (auto& module_path : module_paths) {
        LoadedModule module(
            module_path,
            transport_handle,
            function_loader,
            num_extensions,
            extensions.data()
        );
        if (module.is_enabled()) {
            modules.emplace_back(std::move(module));
        }
    }
#else
    std::vector<std::unique_ptr<ServerModule>> raw_modules = get_static_server_modules(
        transport_handle,
        function_loader,
        num_extensions,
        extensions.data()
    );

    for (auto& raw_module : raw_modules) {
        modules.emplace_back(LoadedModule(
            std::move(raw_module)
        ));
    }
#endif

    return modules;
}

} // namespace xrtransport