#include "kbd/ProfileMemory.h"

namespace kbd {

bool GlobalTravelConfig::isRapidTrigger() const noexcept {
    return mode == MODE_RAPID;
}

std::optional<GlobalTravelConfig> ProfileMemory::decodeGlobalTravel(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < GLOBAL_TRAVEL_SIZE) return std::nullopt;

    GlobalTravelConfig config;
    config.mode              = bytes[0] & 0x03;
    config.actuationTenths   = bytes[0] >> 2;
    config.pressSensTenths   = bytes[1] & 0x3F;
    config.releaseSensTenths = static_cast<std::uint8_t>((bytes[1] >> 6) | ((bytes[2] & 0x0F) << 2));
    config.advancedMode      = bytes[2] >> 4;
    config.advancedData      = bytes[3];
    return config;
}

std::array<std::uint8_t, ProfileMemory::GLOBAL_TRAVEL_SIZE> ProfileMemory::encodeGlobalTravel(const GlobalTravelConfig& config) noexcept {
    const unsigned mode    = config.mode & 0x03u;
    const unsigned act     = config.actuationTenths & 0x3Fu;
    const unsigned press   = config.pressSensTenths & 0x3Fu;
    const unsigned release = config.releaseSensTenths & 0x3Fu;
    const unsigned advMode = config.advancedMode & 0x0Fu;

    return {
        static_cast<std::uint8_t>(mode | (act << 2)),
        static_cast<std::uint8_t>(press | ((release & 0x03u) << 6)),
        static_cast<std::uint8_t>((release >> 2) | (advMode << 4)),
        config.advancedData,
    };
}

} // namespace kbd
