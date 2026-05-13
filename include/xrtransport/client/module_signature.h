#ifndef XRTRANSPORT_CLIENT_MODULE_SIGNATURE_H
#define XRTRANSPORT_CLIENT_MODULE_SIGNATURE_H

#include "module_interface.h"
#include "xrtransport/api.h"

extern "C" {

/**
 * Entry point for dynamically linked client modules. Returns a newly allocated ClientModule instance,
 * which the caller takes ownership of.
 */
XRTP_API_EXPORT xrtransport::ClientModule* xrtp_get_client_module(xrtp_Transport transport);

} // extern "C"

#endif // XRTRANSPORT_CLIENT_MODULE_SIGNATURE_H