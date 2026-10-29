#pragma once

#include <stdexcept>
#include <string>

namespace kbd {

    class KbdException : public std::runtime_error {
    public:
        explicit KbdException(const std::string& message) : std::runtime_error(message) {}
    };

    class DeviceException : public KbdException {
    public:
        explicit DeviceException(const std::string& message) : KbdException("[Device] " + message) {}
    };

    class ProtocolException : public KbdException {
    public:
        explicit ProtocolException(const std::string& message) : KbdException("[Protocol] " + message) {}
    };

    class ConfigException : public KbdException {
    public:
        explicit ConfigException(const std::string& message) : KbdException("[Config] " + message) {}
    };

} // namespace kbd