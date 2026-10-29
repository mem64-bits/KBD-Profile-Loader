#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <hidapi.h>

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
        // Add to public methods in headers/kbd/KeyboardDevice.h:
        void flushInputBuffer() const;

    private:
        struct CandidateEndpoint {
            std::string path;
            uint16_t    productId;
            int         interfaceNumber;
        };

        std::vector<CandidateEndpoint> findAllCandidates();
        bool probeEndpoint(hid_device* handle);

        mutable std::mutex m_ioMutex; // Guards m_handle and all HID operations
        hid_device*        m_handle = nullptr;
        std::string        m_devicePath{};
        bool               m_isConnected = false;
        bool               m_dryRun = false;
        std::string        m_dumpFilePath = "dumped_packets.txt";
    };

} // namespace kbd