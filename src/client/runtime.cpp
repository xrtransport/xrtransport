// SPDX-License-Identifier: LGPL-3.0-or-later

#include "runtime.h"

#include "xrtransport/config/config.h"

#include "asio.hpp"
#include "openxr/openxr.h"

#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
#include "GoldfishAddressSpaceStream.h"
using gfxstream::guest::IOStream;
#endif

#include <stdexcept>
#include <iostream>
#include <thread>
#include <cstdint>

using asio::ip::tcp;
using asio::local::stream_protocol;
using std::uint64_t;
using std::uint32_t;

using namespace xrtransport::configuration;

namespace xrtransport {

static asio::io_context io_context;
static std::unique_ptr<Runtime> runtime;
static std::unique_ptr<Config> config;

static bool do_handshake(SyncDuplexStream& stream) {
    // handle magic
    uint32_t client_magic = XRTRANSPORT_MAGIC;
    asio::write(stream, asio::buffer(&client_magic, sizeof(uint32_t)));
    uint32_t server_magic{};
    asio::read(stream, asio::buffer(&server_magic, sizeof(uint32_t)));
    if (client_magic != server_magic) {
        stream.close();
        return false;
    }

    // write client's version numbers
    uint64_t client_xr_api_version = XR_CURRENT_API_VERSION;
    asio::write(stream, asio::buffer(&client_xr_api_version, sizeof(uint64_t)));
    uint32_t client_xrtransport_protocol_version = XRTRANSPORT_PROTOCOL_VERSION;
    asio::write(stream, asio::buffer(&client_xrtransport_protocol_version, sizeof(uint32_t)));

    // read server's version numbers
    uint64_t server_xr_api_version{};
    asio::read(stream, asio::buffer(&server_xr_api_version, sizeof(uint64_t)));
    uint32_t server_xrtransport_protocol_version{};
    asio::read(stream, asio::buffer(&server_xrtransport_protocol_version, sizeof(uint32_t)));

    // for now, only allow exact match
    uint32_t client_ok =
        client_xr_api_version == server_xr_api_version &&
        client_xrtransport_protocol_version == server_xrtransport_protocol_version;
    asio::write(stream, asio::buffer(&client_ok, sizeof(uint32_t)));
    if (!client_ok) {
        stream.close();
        return false;
    }

    uint32_t server_ok{};
    asio::read(stream, asio::buffer(&server_ok, sizeof(uint32_t)));
    if (!server_ok) {
        stream.close();
        return false;
    }

    return true;
}

static std::unique_ptr<SyncDuplexStream> create_tcp_connection(std::string ip, uint16_t port) {
    tcp::socket socket(io_context);

    // Resolve the server address
    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(ip, std::to_string(port));

    // Connect to the server
    asio::connect(socket, endpoints);
    socket.set_option(tcp::no_delay(true));

    // Wrap the socket in a DuplexStream
    return std::make_unique<SyncDuplexStreamImpl<tcp::socket>>(std::move(socket));
}

static std::unique_ptr<SyncDuplexStream> create_unix_connection(std::string path) {
#ifdef ASIO_HAS_LOCAL_SOCKETS
    stream_protocol::socket socket(io_context);
    socket.connect(stream_protocol::endpoint(path));

    return std::make_unique<SyncDuplexStreamImpl<stream_protocol::socket>>(std::move(socket));
#else
    throw std::runtime_error("Unix sockets not supported");
#endif
}

static std::unique_ptr<SyncDuplexStream> create_asg_connection() {
#ifdef XRTRANSPORT_BUILD_FOR_GFXSTREAM
    class GfxstreamSyncDuplexStream : public SyncDuplexStream {
    private:
        std::unique_ptr<IOStream> wrapped;
    public:
        explicit GfxstreamSyncDuplexStream(std::unique_ptr<IOStream> wrapped)
        : wrapped(std::move(wrapped)) {}

        size_t read_some(const asio::mutable_buffer& buffer, asio::error_code& ec) override {
            ec.clear();

            if (!wrapped) {
                ec = asio::error::eof;
                return 0;
            }

            size_t inout_len = buffer.size();
            if (!wrapped->read(buffer.data(), &inout_len)) {
                // gfxstream doesn't tell you which kind of error it was, so just report EOF
                ec = asio::error::eof;
            }

            return inout_len;
        }

        size_t write_some(const asio::const_buffer& buffer, asio::error_code& ec) override {
            ec.clear();

            if (!wrapped) {
                ec = asio::error::eof;
                return 0;
            }

            // IOStream doesn't support partial writes so just write fully
            if (wrapped->writeFully(buffer.data(), buffer.size()) == -1) {
                // gfxstream doesn't tell you which kind of error it was, so just report EOF
                ec = asio::error::eof;

                // no way to know how much was written on failure
                return 0;
            }

            return buffer.size();
        }

        void close(asio::error_code& ec) override {
            // IOStream doesn't expose a close method, but based on AddressSpaceStream, it looks
            // like this is what the destructor is for
            ec.clear();
            wrapped.reset();
        }
    };

    auto stream = std::unique_ptr<AddressSpaceStream>(createGoldfishAddressSpaceStream(0));

    auto wrapped_stream = std::make_unique<GfxstreamSyncDuplexStream>(std::move(stream));

    // send special opcode that will make xrtransport take over the host RenderThread
    // TODO: factor out the magic number

    uint32_t xrtransport_packet[2] {
        5892, // opcode (outside existing ranges for Vulkan, GLES, etc.)
        8, // length (only this header)
    };

    asio::write(wrapped_stream, asio::buffer(xrtransport_packet, sizeof(xrtransport_packet)));
#else
    throw std::runtime_error("ASG streams not supported");
#endif
}

std::unique_ptr<SyncDuplexStream> create_connection() {
    if (runtime) {
        throw std::runtime_error("Connection already exists");
    }

    if (!config) {
        config = std::make_unique<Config>(load_config());
    }

    if (config->transport_type == TransportType::TCP) {
        return create_tcp_connection(config->ip_address, config->port);
    }
    else if (config->transport_type == TransportType::UNIX) {
        return create_unix_connection(config->unix_path);
    }
    else if (config->transport_type == TransportType::ASG) {
        return create_asg_connection();
    }
    else {
        throw std::runtime_error("Invalid transport type");
    }
}

Runtime& get_runtime() {
    // Lazy initialization
    if (!runtime) {
        // Create stream
        auto stream = create_connection();

        // Do the initial handshake
        if (!do_handshake(*stream)) {
            throw std::runtime_error("Transport handshake failed");
        }

        // Create the Transport instance
        runtime = std::make_unique<Runtime>(std::move(stream));

        // Start Transport thread
        runtime->get_transport().start();
    }

    return *runtime;
}

} // namespace xrtransport