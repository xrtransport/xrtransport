// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_MODULE_H
#define XRTRANSPORT_SERVER_MODULE_H

#include "xrtransport/server/module_signature.h"

#ifdef _WIN32
    #include <windows.h>
    #define MODULE_HANDLE HMODULE
    #define MODULE_LOAD(path) LoadLibraryA(path)
    #define MODULE_SYM(handle, name) GetProcAddress(handle, name)
    #define MODULE_UNLOAD(handle) FreeLibrary(handle)
    #define MODULE_EXT ".dll"
    #define MODULE_NULL NULL
#else
    #include <dlfcn.h>
    #define MODULE_HANDLE void*
    #define MODULE_LOAD(path) dlopen(path, RTLD_LAZY)
    #define MODULE_SYM(handle, name) dlsym(handle, name)
    #define MODULE_UNLOAD(handle) dlclose(handle)
    #define MODULE_EXT ".so"
    #define MODULE_NULL nullptr
#endif

#include "xrtransport/transport/transport.h"
#include "xrtransport/server/function_loader.h"
#include "openxr/openxr.h"

#include <memory>

namespace xrtransport {

// Module function signatures
typedef decltype(&xrtp_get_server_module) PFN_get_server_module;

class LoadedModule {
private:
    MODULE_HANDLE handle;
    std::unique_ptr<ServerModule> module;

public:
    // constructor for dynamically linked modules
    explicit LoadedModule(
        std::string module_path,
        xrtp_Transport transport,
        FunctionLoader* function_loader,
        std::uint32_t num_extensions,
        const XrExtensionProperties* extensions
    ) {
        handle = MODULE_LOAD(module_path.c_str());
        auto pfn_get_server_module = reinterpret_cast<PFN_get_server_module>(MODULE_SYM(handle, "xrtp_get_server_module"));
        auto p_module = pfn_get_server_module(transport, function_loader, num_extensions, extensions);
        module = std::unique_ptr<ServerModule>(p_module);
    }

    // constructor for statically linked modules
    explicit LoadedModule(std::unique_ptr<ServerModule> module) : handle(MODULE_NULL), module(std::move(module)) {}

    ~LoadedModule() {
        // handle may be null if module was moved
        if (handle) {
            MODULE_UNLOAD(handle);
        }
    }

    LoadedModule(const LoadedModule&) = delete;
    LoadedModule& operator=(const LoadedModule&) = delete;

    LoadedModule(LoadedModule&& other) noexcept {
        handle = other.handle;
        module = std::move(other.module);
        other.handle = MODULE_NULL;
        other.module = nullptr;
    }

    LoadedModule& operator=(LoadedModule&& other) noexcept {
        if (&other == this) return *this;

        if (handle) {
            MODULE_UNLOAD(handle);
        }
        handle = other.handle;

        module = std::move(other.module);

        return *this;
    }

    void get_required_extensions(
        std::uint32_t* num_extensions_out,
        const char** extensions_out)
    {
        return module->get_required_extensions(num_extensions_out, extensions_out);
    }

    void on_instance(XrInstance instance)
    {
        return module->on_instance(instance);
    }

    void on_instance_destroy() {
        return module->on_instance_destroy();
    }

    bool is_enabled() const {
        return module.get() != nullptr;
    }
};

} // namespace xrtransport

#endif // XRTRANSPORT_SERVER_MODULE_H