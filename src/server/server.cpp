// SPDX-License-Identifier: LGPL-3.0-or-later

#include "server.h"

#include "module.h"
#include "module_loader.h"

#include "xrtransport/transport/transport.h"
#include "xrtransport/server/function_loader.h"
#include "xrtransport/serialization/serializer.h"
#include "xrtransport/serialization/deserializer.h"
#include "xrtransport/asio_compat.h"
#include "xrtransport/time.h"

#include "openxr/openxr.h"

#include <cstdint>
#include <cstring>

using std::uint32_t;
using std::uint64_t;

namespace xrtransport {

Server::Server(std::unique_ptr<SyncDuplexStream> stream) :
    transport(std::move(stream)),
    function_loader(xrGetInstanceProcAddr),
    function_dispatch(
        transport,
        function_loader,
        [this](MessageLockIn msg_in){create_instance_handler(std::move(msg_in));},
        [this](MessageLockIn msg_in){destroy_instance_handler(std::move(msg_in));}
    ),
    modules(ServerModuleLoader::load_modules(transport.get_handle(), &function_loader))
{

}

// static
bool Server::do_handshake(SyncDuplexStream& stream) {
    uint32_t client_magic{};
    asio::read(stream, asio::buffer(&client_magic, sizeof(uint32_t)));
    uint32_t server_magic = XRTRANSPORT_MAGIC;
    // make sure magic matches
    if (server_magic != client_magic) {
        stream.close();
        return false;
    }
    asio::write(stream, asio::buffer(&server_magic, sizeof(uint32_t)));
    
    // read version numbers from client
    uint64_t client_xr_api_version{};
    asio::read(stream, asio::buffer(&client_xr_api_version, sizeof(uint64_t)));
    uint32_t client_xrtransport_protocol_version{};
    asio::read(stream, asio::buffer(&client_xrtransport_protocol_version, sizeof(uint32_t)));

    // write server's version numbers
    uint64_t server_xr_api_version = XR_CURRENT_API_VERSION;
    asio::write(stream, asio::buffer(&server_xr_api_version, sizeof(uint64_t)));
    uint32_t server_xrtransport_protocol_version = XRTRANSPORT_PROTOCOL_VERSION;
    asio::write(stream, asio::buffer(&server_xrtransport_protocol_version, sizeof(uint32_t)));

    uint32_t client_ok{};
    asio::read(stream, asio::buffer(&client_ok, sizeof(uint32_t)));
    if (!client_ok) {
        stream.close();
        return false;
    }

    // for now, only allow exact match
    uint32_t server_ok =
        client_xr_api_version == server_xr_api_version &&
        client_xrtransport_protocol_version == server_xrtransport_protocol_version;
    asio::write(stream, asio::buffer(&server_ok, sizeof(uint32_t)));
    if (!server_ok) {
        stream.close();
        return false;
    }

    return true;
}

void Server::run() {
    transport.register_handler(XRTP_MSG_FUNCTION_CALL, [this](MessageLockIn msg_in){
        uint32_t function_id{};
        asio::read(msg_in.buffer, asio::buffer(&function_id, sizeof(uint32_t)));
        function_dispatch.handle_function(function_id, std::move(msg_in));
    });

    transport.register_handler(XRTP_MSG_SYNCHRONIZATION_REQUEST, [this](MessageLockIn msg_in) {
        // load timer functions
#ifdef _WIN32
        function_loader.ensure_function_loaded(
            "xrConvertWin32PerformanceCounterToTimeKHR",
            from_platform_time);
        function_loader.ensure_function_loaded(
            "xrConvertTimeToWin32PerformanceCounterKHR",
            to_platform_time);
#else
        function_loader.ensure_function_loaded(
            "xrConvertTimespecTimeToTimeKHR",
            from_platform_time);
        function_loader.ensure_function_loaded(
            "xrConvertTimeToTimespecTimeKHR",
            to_platform_time);
#endif

        // read incoming time
        XrTime client_time{};
        asio::read(msg_in.buffer, asio::buffer(&client_time, sizeof(XrTime)));

        // get server time
        XRTRANSPORT_PLATFORM_TIME server_platform_time{};
        get_platform_time(&server_platform_time);
        XrTime server_time{};
        from_platform_time(saved_instance, &server_platform_time, &server_time);

        auto msg_out = transport.start_message(XRTP_MSG_SYNCHRONIZATION_RESPONSE);
        asio::write(msg_out.buffer, asio::buffer(&server_time, sizeof(XrTime)));
        msg_out.flush();
    });

    transport.register_handler(XRTP_MSG_POLL_EVENT, [this](MessageLockIn msg_in){
        function_loader.ensure_function_loaded("xrPollEvent", function_loader.PollEvent);

        while (true) {
            XrEventDataBuffer event_buffer{XR_TYPE_EVENT_DATA_BUFFER};
            XrResult result = function_loader.PollEvent(saved_instance, &event_buffer);

            if (result != XR_SUCCESS) {
                // result is either XR_EVENT_UNAVAILABLE or an error, in either case don't send the
                // data buffer back.
                auto msg_out = transport.start_message(XRTP_MSG_POLL_EVENT_RETURN);
                SerializeContext s_ctx(msg_out.buffer);
                serialize(&result, s_ctx);
                msg_out.flush();
                return;
            }

            // Whether spec compliant or not, it appears that OpenXR runtimes may return events from
            // extensions that are not enabled by the application. We cannot serialize these if the
            // serializer was not compiled for them. The expected behavior for an application
            // receiving an event that the developer wasn't expecting is that it will just be
            // ignored. So, if the serializer was not built for a certain event, we will do the best
            // we can to silently ignore it and not pass it back to the client.

            if (!size_lookup(event_buffer.type)) {
                // The serializer was not built for this event. Skip it and try to get another event
                // to send back to the client
                spdlog::warn("Dropping event with unknown type: {}", (int)event_buffer.type);
                continue;
            }

            auto msg_out = transport.start_message(XRTP_MSG_POLL_EVENT_RETURN);

            // skip_unknown_structs = true to skip any unknown structs in the next chain with a
            // warning instead of throwing an error
            SerializeContext s_ctx(msg_out.buffer, true);
            serialize(&result, s_ctx);
            serialize_xr(&event_buffer, s_ctx);
            msg_out.flush();
            return;
        }
    });

    // let transport run until it closes
    transport.start();
    transport.join();

    // Once handler loop terminates, destroy the instance if the client didn't
    if (saved_instance) {
        xrDestroyInstance(saved_instance);
        saved_instance = XR_NULL_HANDLE;
    }
    function_loader = FunctionLoader(xrGetInstanceProcAddr); // clear all saved functions
}

void Server::create_instance_handler(MessageLockIn msg_in) {
    function_loader.ensure_function_loaded("xrCreateInstance", function_loader.CreateInstance);
    
    // Read in args sent by client
    DeserializeContext d_ctx(msg_in.buffer);
    XrInstanceCreateInfo* createInfo{};
    deserialize_ptr(&createInfo, d_ctx);
    XrInstance* instance{};
    deserialize_ptr(&instance, d_ctx);

    // Put existing extensions into vector
    uint32_t old_enabled_extension_count = createInfo->enabledExtensionCount;
    const char* const* old_enabled_extension_names = createInfo->enabledExtensionNames;
    std::vector<const char*> enabled_extensions(old_enabled_extension_names, old_enabled_extension_names + old_enabled_extension_count);

    // Collect requested extensions from modules
    for (auto& module : modules) {
        // Get count to resize vector
        uint32_t num_extensions{};
        module.get_required_extensions(&num_extensions, nullptr);

        // Get pointer to end of vector and resize
        auto old_size = enabled_extensions.size();
        enabled_extensions.resize(old_size + num_extensions);

        // Fill in new slots
        module.get_required_extensions(&num_extensions, enabled_extensions.data() + old_size);
    }

    // Request timer extension
#ifdef _WIN32
    enabled_extensions.push_back("XR_KHR_win32_convert_performance_counter_time");
#else
    enabled_extensions.push_back("XR_KHR_convert_timespec_time");
#endif

    // Update createInfo
    createInfo->enabledExtensionCount = enabled_extensions.size();
    createInfo->enabledExtensionNames = enabled_extensions.data();

    // Call xrCreateInstance
    XrTime start_time = get_time();
    XrResult _result = function_loader.CreateInstance(createInfo, instance);
    XrDuration runtime_duration = get_time() - start_time;

    if (XR_SUCCEEDED(_result)) {
        saved_instance = *instance;
        function_loader.loader_instance = saved_instance;

        // Notify modules that XrInstance was created
        for (auto& module : modules) {
            module.on_instance(*instance);
        }
    }

    // Send response to client
    // Note: we do this after notifying modules to avoid a race condition
    // Server modules need to be fully initialized before the client returns from xrCreateInstance
    auto msg_out = transport.start_message(XRTP_MSG_FUNCTION_RETURN);
    SerializeContext s_ctx(msg_out.buffer);
    serialize(&_result, s_ctx);
    serialize(&runtime_duration, s_ctx);
    serialize_ptr(instance, 1, s_ctx);
    msg_out.flush();

    // Restore createInfo to make sure cleanup works as expected
    createInfo->enabledExtensionCount = old_enabled_extension_count;
    createInfo->enabledExtensionNames = old_enabled_extension_names;

    // Cleanup from deserializer
    cleanup_ptr(createInfo, 1);
    cleanup_ptr(instance, 1);
}

void Server::destroy_instance_handler(MessageLockIn msg_in) {
    function_loader.ensure_function_loaded("xrDestroyInstance", function_loader.DestroyInstance);
    DeserializeContext d_ctx(msg_in.buffer);
    XrInstance instance{};
    deserialize(&instance, d_ctx);

    XrResult _result;
    XrDuration runtime_duration;
    if (instance == saved_instance) {
        for (auto& module : modules) {
            module.on_instance_destroy();
        }
        XrTime start_time = get_time();
        _result = function_loader.DestroyInstance(instance);
        runtime_duration = get_time() - start_time;
        saved_instance = XR_NULL_HANDLE;
    }
    else {
        _result = XR_ERROR_HANDLE_INVALID;
        runtime_duration = 0;
    }
    
    auto msg_out = transport.start_message(XRTP_MSG_FUNCTION_RETURN);
    SerializeContext s_ctx(msg_out.buffer);
    serialize(&_result, s_ctx);
    serialize(&runtime_duration, s_ctx);
    msg_out.flush();
}

}