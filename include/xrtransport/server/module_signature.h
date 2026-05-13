// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_MODULE_SIGNATURE_H
#define XRTRANSPORT_SERVER_MODULE_SIGNATURE_H

#include "module_interface.h"
#include "xrtransport/api.h"

extern "C" {

/**
 * Returns a newly allocated ServerModule instance, which the caller takes ownership of.
 * May return nullptr if the required extensions are not present, or for any other reason.
 * 
 * This is the main entry point for dynamically linked modules.
 */
XRTP_API_EXPORT xrtransport::ServerModule* xrtp_get_server_module(
    xrtp_Transport transport,
    xrtransport::FunctionLoader* function_loader,
    std::uint32_t num_extensions,
    const XrExtensionProperties* extensions);

} // extern "C"

#endif // XRTRANSPORT_SERVER_MODULE_SIGNATURE_H