#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace kbd {

    // The "global" travel settings at the start of a Keychron analog-matrix profile
    // (`analog_key_config_t global`, Keychron/qmk_firmware: keyboards/keychron/common/analog_matrix).
    // The 860-byte profile dump handled by KeychronProtocol begins with these 4 packed bytes:
    //
    //   byte 0: bits 0-1 mode            bits 2-7 actuation point (0.1 mm)
    //   byte 1: bits 0-5 press sens.     bits 6-7 release sens. (low 2 bits)
    //   byte 2: bits 0-3 release sens. (high 4 bits)   bits 4-7 advanced mode
    //   byte 3: advanced-mode data
    struct GlobalTravelConfig {
        static constexpr std::uint8_t MODE_GLOBAL  = 0; // key follows the profile's global mode
        static constexpr std::uint8_t MODE_REGULAR = 1; // static actuation point
        static constexpr std::uint8_t MODE_RAPID   = 2; // Rapid Trigger

        std::uint8_t mode{MODE_REGULAR};
        std::uint8_t actuationTenths{0};   // 0.1 mm units; 0 = "use the firmware default"
        std::uint8_t pressSensTenths{0};   // Rapid Trigger press sensitivity, 0.1 mm units
        std::uint8_t releaseSensTenths{0}; // Rapid Trigger release sensitivity; 0 = same as press
        std::uint8_t advancedMode{0};
        std::uint8_t advancedData{0};

        [[nodiscard]] bool isRapidTrigger() const noexcept;

        bool operator==(const GlobalTravelConfig&) const noexcept = default;
    };

    class ProfileMemory {
    public:
        static constexpr std::size_t GLOBAL_TRAVEL_OFFSET = 0;
        static constexpr std::size_t GLOBAL_TRAVEL_SIZE   = 4;

        // Needs at least GLOBAL_TRAVEL_SIZE bytes; extra bytes are ignored.
        [[nodiscard]] static std::optional<GlobalTravelConfig> decodeGlobalTravel(std::span<const std::uint8_t> bytes) noexcept;

        // Inverse of decodeGlobalTravel (values are masked to their field widths).
        [[nodiscard]] static std::array<std::uint8_t, GLOBAL_TRAVEL_SIZE> encodeGlobalTravel(const GlobalTravelConfig& config) noexcept;
    };

} // namespace kbd
