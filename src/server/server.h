// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_SERVER_H
#define XRTRANSPORT_SERVER_H

#include "xrtransport/transport/transport.h"
#include "xrtransport/server/function_loader.h"
#include "xrtransport/time.h"

#include "module.h"
#include "function_dispatch.h"

#include "asio/io_context.hpp"

#include <vector>
#include <string>
#include <memory>

namespace xrtransport {

class Server {
private:
    Transport transport;
    FunctionLoader function_loader;
    FunctionDispatch function_dispatch;
    std::vector<LoadedModule> modules;
    XrInstance saved_instance;

    // Custom handler of xrCreateInstance provided to FunctionDispatch via dependency injection
    void create_instance_handler(MessageLockIn msg_in);

    void destroy_instance_handler(MessageLockIn msg_in);

    // function pointers to runtime's timer functions
    // this works because the functions have the same signature on both platforms
    XrResult (*from_platform_time)(XrInstance instance, const XRTRANSPORT_PLATFORM_TIME* platform_time, XrTime* time) = nullptr;
    XrResult (*to_platform_time)(XrInstance instance, XrTime time, XRTRANSPORT_PLATFORM_TIME* platform_time) = nullptr;

public:
    explicit Server(std::unique_ptr<SyncDuplexStream> stream);

    static bool do_handshake(SyncDuplexStream& stream);

    void run();
};

} // namespace xrtransport

#endif // XRTRANSPORT_SERVER_H