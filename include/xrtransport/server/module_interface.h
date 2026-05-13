// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_MODULE_INTERFACE_H
#define XRTRANSPORT_SERVER_MODULE_INTERFACE_H

#include "xrtransport/transport/transport.h"
#include "xrtransport/server/function_loader.h"
#include "openxr/openxr.h"

namespace xrtransport {

class ServerModule {
public:
    /**
     * Mechanism used by the server to know what extensions to request when creating the runtime.
     * 
     * Uses a double-call idiom:
     * Call first will null extensions_out to know how much space to allocate via num_extensions_out.
     * Then call again to populate strings via extensions_out.
     */
    virtual void get_required_extensions(
        std::uint32_t* num_extensions_out,
        const char** extensions_out
    ) const = 0;

    /**
     * Called immediately after the xrCreateInstance call completes.
     * Can be used to load functions that require an XrInstance to be loaded.
     */
    virtual void on_instance(
        XrInstance instance
    ) = 0;

    /**
     * Called immediately before the xrDestroyInstance call completes.
     * Should be used to clean up any instance-specific state, as many instances could be created and
     * destroyed over the lifetime of the server.
     */
    virtual void on_instance_destroy() = 0;

    /**
     * Used for any cleanup before a module is unloaded.
     */
    virtual ~ServerModule() = default;
};

} // namespace xrtransport

#endif // XRTRANSPORT_SERVER_MODULE_INTERFACE_H