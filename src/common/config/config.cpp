// SPDX-License-Identifier: LGPL-3.0-or-later

#include "xrtransport/config/config.h"

#include <nlohmann/json.hpp>

#ifdef __ANDROID__
#include <sys/system_properties.h>
#else
// compiled config path must be defined
#ifndef XRTRANSPORT_CONFIG_PATH
#error XRTRANSPORT_CONFIG_PATH must be defined
#endif
#endif

#include <fstream>
#include <cstring>
#include <limits>
#include <sstream>
#include <cstdlib>

using nlohmann::json;

namespace xrtransport {

namespace configuration {

Config from_file(std::string path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return from_json(buffer.str());
}

Config from_json(std::string json_str) {
    Config result;
    try {
        json data = json::parse(json_str);
        auto transport_type = data.at("transport_type").get<std::string>();
        if (transport_type == "tcp") {
            result.transport_type = TransportType::TCP;
            result.ip_address = data.at("ip_address").get<std::string>();
            result.port = data.at("port").get<uint16_t>();
        }
        else if (transport_type == "unix") {
            result.transport_type = TransportType::UNIX;
            result.unix_path = data.at("unix_path").get<std::string>();
        }
        else if (transport_type == "asg") {
            result.transport_type = TransportType::ASG;
        }
        else {
            throw ConfigException("invalid transport_type: " + transport_type);
        }
    }
    catch(const json::exception& e) {
        throw ConfigException(std::string("error parsing json: ") + e.what());
    }

    return result;
}

#ifdef __ANDROID__
static std::string get_system_property(std::string property_name) {
    auto prop = __system_property_find(property_name.c_str());
    if (!prop) {
        throw ConfigException("system property " + property_name + " not set");
    }
    std::string result;
    // callback runs synchronously
    __system_property_read_callback(prop, [](void* cookie, const char*, const char* value, uint32_t){
        auto p_result = reinterpret_cast<std::string*>(cookie);
        *p_result = value;
    }, &result);
    return result;
}

Config from_android_system_properties() {
    Config result;
    std::string transport_type = get_system_property("xrtransport.transport_type");
    if (transport_type == "tcp") {
        result.transport_type = TransportType::TCP;
        result.ip_address = get_system_property("xrtransport.ip_address");
        std::string port_string = get_system_property("xrtransport.port");
        unsigned long port_long;
        try {
            port_long = std::stoul(port_string);
        }
        catch (const std::exception& e) {
            throw ConfigException("xrtransport.port must be a positive number");
        }
        if (port_long > std::numeric_limits<uint16_t>::max()) {
            throw ConfigException("xrtransport.port " + port_string + " out of range");
        }
        result.port = static_cast<uint16_t>(port_long);
    }
    else if (transport_type == "unix") {
        result.transport_type = TransportType::UNIX;
        result.unix_path = get_system_property("xrtransport.unix_path");
    }
    else if (transport_type == "asg") {
        result.transport_type = TransportType::ASG;
    }
    else {
        throw ConfigException("invalid xrtransport.transport_type: " + transport_type);
    }
    return result;
}
#endif

Config load_config() {
#if defined(__ANDROID__)
    return from_android_system_properties();
#elif defined(__linux__)
    const char* config_path = std::getenv("XRTRANSPORT_CONFIG_PATH");
    if (!config_path) {
        config_path = XRTRANSPORT_CONFIG_PATH;
    }
    return from_file(config_path);
#elif defined(_WIN32)
#error TODO: implement
#else
#error Unsupported platform
#endif
}

} // namespace configuration

} // namespace xrtransport