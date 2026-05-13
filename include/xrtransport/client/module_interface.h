// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_CLIENT_MODULE_INTERFACE_H
#define XRTRANSPORT_CLIENT_MODULE_INTERFACE_H

#include "xrtransport/transport/transport_c_api.h"

#include <openxr/openxr.h>

#include <cstdint>

namespace xrtransport {

/**
 * Contains information about how to layer a function onto the dispatch table.
 * When the specified function is applied, the existing value in the table will be
 * written into *old_function, and new_function will replace it in the dispatch table.
 */
struct ModuleLayerFunction {
    const char* function_name;
    PFN_xrVoidFunction new_function;
    PFN_xrVoidFunction* old_function;
};

/**
 * Represents an extension that a module supports. Will be advertised to applications
 * if returned by a module. If the extension is selected, all the functions in function_names
 * will be marked available.
 */
struct ModuleExtension {
    const char* extension_name;
    uint32_t extension_version;
    uint32_t num_functions;
    const char* const* function_names;
};

typedef void (*ModuleInstanceCallback)(XrInstance instance, PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr);

/**
 * Used to return all the necessary data for a module in a single operation.
 */
struct ModuleInfo {
    uint32_t num_extensions;
    const ModuleExtension* extensions;
    uint32_t num_functions;
    const ModuleLayerFunction* functions;
};

class ClientModule {
public:
    /**
     * This is called by the runtime to fetch information about which extensions to advertise to the application,
     * and which functions to layer onto the function table.
     * 
     * Any new function you want to expose to an application *must* be included in a ModuleExtension. The runtime
     * tracks available functions based on which extensions the application has enabled.
     * 
     * Any function you wish to override must be provided as a ModuleLayerFunction, with the new_function and
     * an address to store the old_function, which the new_function may call to continue down the layers.
     * 
     * This function must return a pointer to a ModuleInfo struct that contains all of this. The ModuleInfo and
     * all data it references must have a static storage lifetime -- no attempt to clean it up will be made.
     */
    virtual const ModuleInfo* get_module_info() = 0;
    virtual void on_instance(XrInstance instance, PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr) = 0;

    virtual ~ClientModule() = default;
};

} // namespace xrtransport

#endif // XRTRANSPORT_CLIENT_MODULE_INTERFACE_H