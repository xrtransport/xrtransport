// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef XRTRANSPORT_CONFIG_H
#define XRTRANSPORT_CONFIG_H

#include <string>
#include <cstdint>
#include <stdexcept>

namespace xrtransport {

namespace configuration {

// Exception for invalid configuration
class ConfigException : public std::runtime_error {
public:
    explicit ConfigException(const std::string& message) : std::runtime_error(message) {}
};

enum class TransportType {
    TCP,
    UNIX,
    ASG
};

struct Config {
    TransportType transport_type;

    // Filled in for TransportType::TCP
    std::string ip_address;
    std::uint16_t port;

    // Filled in for TransportType::UNIX
    std::string unix_path;
};

// Read and parse JSON file
Config from_file(std::string path);

// Parse config from JSON
Config from_json(std::string json_str);

#ifdef __ANDROID__
// Read config from Android system properties
Config from_android_system_properties();
#endif

// Load config from default location.
// uses system properties on Android, otherwise the compiled path in XRTRANSPORT_CONFIG_PATH
Config load_config();

} // namespace configuration

} // namespace xrtransport

#endif // XRTRANSPORT_CONFIG_H