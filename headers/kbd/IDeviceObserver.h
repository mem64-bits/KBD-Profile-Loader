#pragma once
#include <string>

namespace kbd {

    // How the keyboard is attached. Only the USB-C cable carries the raw-HID configuration interface on the
    // K2 HE, so the app talks to it wired only (2.4 GHz and Bluetooth are left to the keyboard itself).
    enum class TransportMode {
        Disconnected,
        Wired
    };

    // Which K2 HE it is, from the USB product id (keyboards/keychron/k2_he/*/keyboard.json).
    enum class KeyboardVariant {
        Unknown,
        Ansi, // 0x0E20
        Iso,  // 0x0E21
        Jis   // 0x0E22
    };

    class IDeviceObserver {
    public:
        virtual ~IDeviceObserver() = default;
        virtual void onDeviceConnected(TransportMode mode, const std::string& path) = 0;
        virtual void onDeviceDisconnected() = 0;
    };

} // namespace kbd