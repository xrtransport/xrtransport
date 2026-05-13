// SPDX-License-Identifier: LGPL-3.0-or-later

#include "client_handle_exchange_factory.h"
#include "xrtransport/client/module_interface.h"
#include "xrtransport/client/module_signature.h"
#include "xrtransport/handle_exchange.h"
#include "messages.h"

#include "xrtransport/serialization/serializer.h"
#include "xrtransport/serialization/deserializer.h"
#include "xrtransport/util.h"

// we want to import symbols from here, not export them
#undef XRTRANSPORT_EXPORT_API
#include "xrtransport/transport/transport.h"

#include <openxr/openxr.h>
#include <spdlog/spdlog.h>

#include <sys/socket.h>
#include <memory>

using namespace xrtransport;

namespace {

// stored globally, set by the single HandleExchangeClientModule instance
int socket_fd = -1;

class HandleExchangeClientModule : public ClientModule {
private:
    Transport transport;
public:
    explicit HandleExchangeClientModule(xrtp_Transport transport_handle)
    : transport(transport_handle) {}

    const ModuleInfo* get_module_info() override {
        static ModuleInfo module_info = {
            .num_extensions = 0,
            .extensions = nullptr,
            .num_functions = 0,
            .functions = nullptr,
        };
        return &module_info;
    }

    void on_instance(XrInstance instance, PFN_xrGetInstanceProcAddr pfn_xrGetInstanceProcAddr) override {
        auto msg_out = transport.start_message(XRTP_MSG_HANDLE_EXCHANGE_LINUX_GET_PATH);
        msg_out.flush();

        auto msg_in = transport.await_message(XRTP_MSG_HANDLE_EXCHANGE_LINUX_RETURN_PATH);
        DeserializeContext d_ctx(msg_in.buffer);
        const char* socket_path{};
        deserialize_ptr(&socket_path, d_ctx);

        socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            spdlog::error("Failed to create handle exchange socket: {}", errno);
            return;
        }

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
        cleanup_ptr(socket_path, count_null_terminated(socket_path));

        // send a message to tell the server to call accept
        msg_out = transport.start_message(XRTP_MSG_HANDLE_EXCHANGE_LINUX_CLIENT_CONNECTING);
        msg_out.flush();

        if (connect(socket_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            spdlog::error("Failed to connect handle exchange socket to path: {}, errno: {}", socket_path, errno);
            return;
        }
    }
};

} // namespace

namespace xrtransport {

std::unique_ptr<ClientModule> HandleExchangeClientModuleFactory::create(xrtp_Transport transport) {
    return std::make_unique<HandleExchangeClientModule>(transport);
}

} // namespace xrtransport

#ifdef XRTRANSPORT_DYNAMIC_MODULES

// dynamic module entry point
ClientModule* xrtp_get_client_module(xrtp_Transport transport) {
    return HandleExchangeClientModuleFactory::create(transport).release();
}

#endif

xrtp_Handle xrtp_read_handle() {
    if (socket_fd < 0) {
        spdlog::error("Handle exchange was not properly created, returning null handle.");
        return 0;
    }

    msghdr msg{};

    char msg_buffer[1]{}; // for dummy byte
    struct iovec io{
        .iov_base = msg_buffer,
        .iov_len = sizeof(msg_buffer)
    };

    char cmsg_buffer[CMSG_SPACE(sizeof(int))]{};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = cmsg_buffer;
    msg.msg_controllen = sizeof(cmsg_buffer);

    if (recvmsg(socket_fd, &msg, 0) < 0) {
        spdlog::error("Error receiving handle exchange message: {}", errno);
        return 0;
    }

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg == nullptr ||
        cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS) {
        spdlog::error("Invalid control message: {}", errno);
        return 0;
    }

    int fd{};
    std::memcpy(&fd, CMSG_DATA(cmsg), sizeof(fd));
    return static_cast<xrtp_Handle>(fd);
}

void xrtp_write_handle(xrtp_Handle handle) {
    if (socket_fd < 0) {
        spdlog::error("Handle exchange was not properly created, not writing handle.");
        return;
    }

    int fd = static_cast<int>(handle);

    msghdr msg{};
    char buf[CMSG_SPACE(sizeof(fd))]{};

    // one byte of dummy data to send with FD
    uint8_t dummy = 0;
    iovec io = {
        .iov_base = &dummy,
        .iov_len = 1
    };

    msg.msg_iov = &io;
    msg.msg_iovlen = 1;

    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));

    if (sendmsg(socket_fd, &msg, 0) == -1) {
        spdlog::error("Failed to send FD via SCM_RIGHTS: {}", errno);
    }

    // now that sendmsg has returned, it's safe to close local fd handle
    close(fd);
}