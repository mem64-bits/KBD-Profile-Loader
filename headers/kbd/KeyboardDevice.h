#pragma once

#include <string>
#include <utility>
#include <span>
#include <optional>
#include <vector>
#include <cstdint>
#include <mutex>
#include <hidapi.h>
#include "kbd/IDeviceObserver.h"

struct hid_device_;
using hid_device = struct hid_device_;

namespace kbd {

    class KeyboardDevice {
    public:
        KeyboardDevice();
        ~KeyboardDevice();

        KeyboardDevice(const KeyboardDevice&) = delete;
        KeyboardDevice& operator=(const KeyboardDevice&) = delete;

        KeyboardDevice(KeyboardDevice&& other) noexcept;
        KeyboardDevice& operator=(KeyboardDevice&& other) noexcept;

        [[nodiscard]] bool connect();
        void disconnect();
        bool sendReport(const std::vector<uint8_t>& packet);
        int readReport(std::vector<uint8_t>& outBuffer, int timeoutMs = 150);

        void setDryRun(bool enabled, const std::string& dumpFilePath = "dumped_packets.txt");
        [[nodiscard]] bool isDryRun() const noexcept;
        [[nodiscard]] bool isConnected() const noexcept;
        [[nodiscard]] std::string getDevicePath() const;
        [[nodiscard]] uint16_t productId() const noexcept;        // 0 while disconnected
        [[nodiscard]] KeyboardVariant variant() const noexcept;   // ANSI / ISO / JIS from the product id
        [[nodiscard]] static KeyboardVariant variantForProductId(uint16_t productId) noexcept;

        // Whether the connected keyboard is still plugged in (enumerates instead of sending traffic).
        [[nodiscard]] bool isPresent() const;

        // Which controller interface the PC enumerated for the connected keyboard, from the HID device list (no
        // traffic): true = XInput, false = HID joystick, nullopt = unknown (not connected, dry run). The firmware
        // decides at enumeration: in XInput mode it cuts the joystick collection (usage page 0x01, usage 0x04)
        // from its shared HID report descriptor, otherwise it drops the XInput interface
        // (analog_matrix/usb_descriptor_override.c). So after a mode change this keeps reporting the old mode
        // until the keyboard is enumerated again.
        [[nodiscard]] std::optional<bool> enumeratedAsXInput() const;
        // The rule above on a list of (usage page, usage) pairs of one keyboard's HID collections.
        [[nodiscard]] static bool hasJoystickCollection(std::span<const std::pair<uint16_t, uint16_t>> usages) noexcept;
        // Add to public methods in headers/kbd/KeyboardDevice.h:
        void flushInputBuffer() const;

    private:
        struct CandidateEndpoint {
            std::string path;
            uint16_t    productId;
            int         interfaceNumber;
        };

        [[nodiscard]] static std::vector<CandidateEndpoint> findAllCandidates();
        bool probeEndpoint(hid_device* handle);

        mutable std::mutex m_ioMutex; // Guards m_handle and all HID operations
        hid_device*        m_handle = nullptr;
        std::string        m_devicePath{};
        uint16_t           m_productId = 0;
        bool               m_isConnected = false;
        bool               m_dryRun = false;
        std::string        m_dumpFilePath = "dumped_packets.txt";
    };

} // namespace kbd