#pragma once
#include <string>

namespace kbd {

    enum class TransportMode {
        Disconnected,
        Wired,
        WirelessDongle
    };

    class IDeviceObserver {
    public:
        virtual ~IDeviceObserver() = default;
        virtual void onDeviceConnected(TransportMode mode, const std::string& path) = 0;
        virtual void onDeviceDisconnected() = 0;
    };

} // namespace kbd