#include "xrtransport/server/server_c_api.h"

#include "server.h"

#include <spdlog/spdlog.h>

#include <system_error>

using namespace xrtransport;

class DelegateSyncDuplexStream : public SyncDuplexStream {
private:
    void* cookie;
    ReadSomeDelegate read_some_delegate;
    WriteSomeDelegate write_some_delegate;
    CloseDelegate close_delegate;

public:
    DelegateSyncDuplexStream(
        void* cookie,
        ReadSomeDelegate read_some_delegate,
        WriteSomeDelegate write_some_delegate,
        CloseDelegate close_delegate)
    : cookie(cookie),
    read_some_delegate(read_some_delegate),
    write_some_delegate(write_some_delegate),
    close_delegate(close_delegate)
    {}

    size_t read_some(const asio::mutable_buffer& buffer, asio::error_code& ec) override {
        ec.clear();
        int error = 0;
        size_t result = read_some_delegate(cookie, buffer.data(), buffer.size(), &error);
        if (error) {
            ec = asio::error_code(error, std::generic_category());
        }
        return result;
    }

    size_t write_some(const asio::const_buffer& buffer, asio::error_code& ec) override {
        ec.clear();
        int error = 0;
        size_t result = write_some_delegate(cookie, buffer.data(), buffer.size(), &error);
        if (error) {
            ec = asio::error_code(error, std::generic_category());
        }
        return result;
    }

    void close(asio::error_code& ec) override {
        ec.clear();
        int error = 0;
        close_delegate(cookie, &error);
        if (error) {
            ec = asio::error_code(error, std::generic_category());
        }
    }
};

bool xrtp_run_server(
    void* cookie,
    ReadSomeDelegate read_some,
    WriteSomeDelegate write_some,
    CloseDelegate close
) {
    std::unique_ptr<SyncDuplexStream> stream =
        std::make_unique<DelegateSyncDuplexStream>(cookie, read_some, write_some, close);

    try {
        if (!Server::do_handshake(*stream)) {
            spdlog::warn("Client handshake failed");
            return false;
        }

        Server server(std::move(stream));

        server.run();
    }
    catch (const std::exception& e) {
        spdlog::error("Connection ended due to error: {}", e.what());
        return false;
    }
    catch (...) {
        spdlog::error("Connection ended due to unknown error");
        return false;
    }

    return true;
}