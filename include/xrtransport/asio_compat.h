// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_ASIO_COMPAT_H
#define XRTRANSPORT_ASIO_COMPAT_H

/*
 * ASIO Compatibility Layer
 *
 * This file provides a compatibility layer between ASIO's template-based approach
 * and an object-oriented approach. It defines abstract base classes that mirror
 * ASIO's stream concepts (ReadableStream, WritableStream, etc.) while providing
 * a virtual interface that can be implemented by concrete stream types.
 *
 * This allows for polymorphic use of different stream types (sockets, buffers,
 * files, etc.) without requiring template instantiation at every call site.
 */

#include <asio.hpp>

#include <cstddef>
#include <functional>

namespace xrtransport {

// Base abstract class for all stream operations
class Stream {
public:
    virtual ~Stream() = default;

    // Stream control
    virtual void close() {
        asio::error_code ec;
        close(ec);
        if (ec) {
            throw asio::system_error(ec);
        }
    }

    virtual void close(asio::error_code& ec) = 0;
};

// Abstract class for synchronous read operations
class SyncReadStream : virtual public Stream {
public:
    virtual ~SyncReadStream() = default;

    // Read some data from the stream
    virtual std::size_t read_some(const asio::mutable_buffer& buffer) {
        asio::error_code ec;
        std::size_t result = read_some(buffer, ec);
        if (ec) {
            throw asio::system_error(ec);
        }
        return result;
    };

    virtual std::size_t read_some(const asio::mutable_buffer& buffer, asio::error_code& ec) = 0;

    // Template convenience methods for ASIO compatibility
    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers) {
        return read_some(asio::mutable_buffer(buffers));
    }

    template<typename MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers, asio::error_code& ec) {
        return read_some(asio::mutable_buffer(buffers), ec);
    }
};

// Abstract class for synchronous write operations
class SyncWriteStream : virtual public Stream {
public:
    virtual ~SyncWriteStream() = default;

    // Write some data to the stream
    virtual std::size_t write_some(const asio::const_buffer& buffer) {
        asio::error_code ec;
        std::size_t result = write_some(buffer, ec);
        if (ec) {
            throw asio::system_error(ec);
        }
        return result;
    };

    virtual std::size_t write_some(const asio::const_buffer& buffer, asio::error_code& ec) = 0;

    // Template convenience methods for ASIO compatibility
    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers) {
        return write_some(asio::const_buffer(buffers));
    }

    template<typename ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers, asio::error_code& ec) {
        return write_some(asio::const_buffer(buffers), ec);
    }
};

// Abstract class for synchronous read/write operations
class SyncDuplexStream : public SyncReadStream, public SyncWriteStream {
public:
    virtual ~SyncDuplexStream() = default;
};

struct Acceptor {
    virtual ~Acceptor() = default;

    virtual std::unique_ptr<SyncDuplexStream> accept() = 0;
};

// Concrete templated implementations

// Concrete implementation of SyncReadStream
template<typename StreamType>
class SyncReadStreamImpl : public SyncReadStream {
private:
    StreamType stream_;

public:
    explicit SyncReadStreamImpl(StreamType stream) : stream_(std::move(stream)) {}

    void close() override {
        stream_.close();
    }

    void close(asio::error_code& ec) override {
        stream_.close(ec);
    }

    std::size_t read_some(const asio::mutable_buffer& buffers) override {
        return stream_.read_some(buffers);
    }

    std::size_t read_some(const asio::mutable_buffer& buffers, asio::error_code& ec) override {
        return stream_.read_some(buffers, ec);
    }
};

// Concrete implementation of SyncWriteStream
template<typename StreamType>
class SyncWriteStreamImpl : public SyncWriteStream {
private:
    StreamType stream_;

public:
    explicit SyncWriteStreamImpl(StreamType stream) : stream_(std::move(stream)) {}

    void close() override {
        stream_.close();
    }

    void close(asio::error_code& ec) override {
        stream_.close(ec);
    }

    std::size_t write_some(const asio::const_buffer& buffers) override {
        return stream_.write_some(buffers);
    }

    std::size_t write_some(const asio::const_buffer& buffers, asio::error_code& ec) override {
        return stream_.write_some(buffers, ec);
    }
};

// Concrete implementation of SyncDuplexStream
template<typename StreamType>
class SyncDuplexStreamImpl : public SyncDuplexStream {
private:
    StreamType stream_;

public:
    explicit SyncDuplexStreamImpl(StreamType stream) : stream_(std::move(stream)) {}

    void close() override {
        stream_.close();
    }

    void close(asio::error_code& ec) override {
        stream_.close(ec);
    }

    std::size_t read_some(const asio::mutable_buffer& buffers) override {
        return stream_.read_some(buffers);
    }

    std::size_t read_some(const asio::mutable_buffer& buffers, asio::error_code& ec) override {
        return stream_.read_some(buffers, ec);
    }

    std::size_t write_some(const asio::const_buffer& buffers) override {
        return stream_.write_some(buffers);
    }

    std::size_t write_some(const asio::const_buffer& buffers, asio::error_code& ec) override {
        return stream_.write_some(buffers, ec);
    }
};

template <typename AcceptorType, typename SocketType>
struct AcceptorImpl : public Acceptor {
private:
    std::reference_wrapper<asio::io_context> io_context;
    AcceptorType acceptor;

public:
    AcceptorImpl(asio::io_context& io_context, AcceptorType acceptor)
         : io_context(io_context), acceptor(std::move(acceptor))
    {}

    std::unique_ptr<SyncDuplexStream> accept() override {
        SocketType socket(io_context.get());
        acceptor.accept(socket);
        return std::make_unique<SyncDuplexStreamImpl<SocketType>>(std::move(socket));
    }
};

} // namespace xrtransport

#endif // XRTRANSPORT_ASIO_COMPAT_H