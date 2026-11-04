#include "kbd/ProfileMemory.h"

#include <algorithm>

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

std::optional<KeyTravelConfig> ProfileMemory::decodeKeyTravel(std::span<const std::uint8_t> profile,
                                                              const std::size_t row, const std::size_t col) noexcept {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return std::nullopt;
    const std::size_t offset = keyConfigOffset(row, col);
    if (profile.size() < offset + TRAVEL_CONFIG_SIZE) return std::nullopt;
    return decodeGlobalTravel(profile.subspan(offset, TRAVEL_CONFIG_SIZE));
}

bool ProfileMemory::encodeKeyTravel(std::span<std::uint8_t> profile, const std::size_t row, const std::size_t col,
                                    const KeyTravelConfig& config) noexcept {
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return false;
    const std::size_t offset = keyConfigOffset(row, col);
    if (profile.size() < offset + TRAVEL_CONFIG_SIZE) return false;

    const auto bytes = encodeGlobalTravel(config);
    for (std::size_t i = 0; i < bytes.size(); ++i) profile[offset + i] = bytes[i];
    return true;
}

std::optional<SocdPair> ProfileMemory::decodeSocd(std::span<const std::uint8_t> profile, const std::size_t index) noexcept {
    if (index >= SOCD_COUNT) return std::nullopt;
    const std::size_t offset = SOCD_OFFSET + index * SOCD_ENTRY_SIZE;
    if (profile.size() < offset + SOCD_ENTRY_SIZE) return std::nullopt;

    SocdPair pair;
    pair.row1 = profile[offset] & 0x07;
    pair.col1 = profile[offset] >> 3;
    pair.row2 = profile[offset + 1] & 0x07;
    pair.col2 = profile[offset + 1] >> 3;
    pair.type = profile[offset + 2];
    return pair;
}

bool ProfileMemory::encodeSocd(std::span<std::uint8_t> profile, const std::size_t index, const SocdPair& pair) noexcept {
    if (index >= SOCD_COUNT) return false;
    const std::size_t offset = SOCD_OFFSET + index * SOCD_ENTRY_SIZE;
    if (profile.size() < offset + SOCD_ENTRY_SIZE) return false;

    profile[offset]     = static_cast<std::uint8_t>((pair.row1 & 0x07) | ((pair.col1 & 0x1F) << 3));
    profile[offset + 1] = static_cast<std::uint8_t>((pair.row2 & 0x07) | ((pair.col2 & 0x1F) << 3));
    profile[offset + 2] = pair.type;
    return true;
}

bool OkmcConfig::isEmpty() const noexcept {
    for (const std::uint16_t keycode : keycodes) {
        if (keycode != 0) return false;
    }
    return true;
}

OkmcConfig deepPressCombo(const std::uint16_t baseKeycode, const std::uint16_t extraKeycode, const std::uint8_t shallowTenths,
                          const std::uint8_t deepTenths) noexcept {
    constexpr std::uint8_t HYSTERESIS = 3; // 0.3 mm, so a key resting on the deep point does not chatter
    OkmcConfig config;
    config.shallowActTenths   = static_cast<std::uint8_t>(std::clamp<int>(shallowTenths, 1, 63));
    config.shallowDeactTenths = config.shallowActTenths;
    config.deepActTenths      = static_cast<std::uint8_t>(std::clamp<int>(deepTenths, config.shallowActTenths + 1, 63));
    config.deepDeactTenths    = static_cast<std::uint8_t>(std::max<int>(config.deepActTenths - HYSTERESIS, config.shallowActTenths));

    config.keycodes[0] = baseKeycode;
    config.actions[0].shallowPress   = OkmcAction::PRESS;   // walk
    config.actions[0].shallowRelease = OkmcAction::RELEASE;

    config.keycodes[1] = extraKeycode;
    config.actions[1].deepPress      = OkmcAction::PRESS;   // run while deep
    config.actions[1].deepRelease    = OkmcAction::RELEASE;
    config.actions[1].shallowRelease = OkmcAction::RELEASE;
    return config;
}

std::array<std::uint8_t, ProfileMemory::OKMC_ENTRY_SIZE> ProfileMemory::encodeOkmcEntry(const OkmcConfig& config) noexcept {
    std::array<std::uint8_t, OKMC_ENTRY_SIZE> bytes{};
    // okmc_traval_config_t: four packed 6-bit fields, least significant first.
    const std::uint32_t travel = (config.shallowActTenths & 0x3Fu) | ((config.shallowDeactTenths & 0x3Fu) << 6) |
                                 ((config.deepActTenths & 0x3Fu) << 12) | ((config.deepDeactTenths & 0x3Fu) << 18);
    bytes[0] = static_cast<std::uint8_t>(travel & 0xFF);
    bytes[1] = static_cast<std::uint8_t>((travel >> 8) & 0xFF);
    bytes[2] = static_cast<std::uint8_t>((travel >> 16) & 0xFF);
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[3 + i * 2] = static_cast<std::uint8_t>(config.keycodes[i] & 0xFF);
        bytes[4 + i * 2] = static_cast<std::uint8_t>(config.keycodes[i] >> 8);
        // okmc_action_t: two nibbles per byte, low nibble first.
        const OkmcAction& action = config.actions[i];
        bytes[11 + i * 2] = static_cast<std::uint8_t>((action.shallowPress & 0x0F) | ((action.shallowRelease & 0x0F) << 4));
        bytes[12 + i * 2] = static_cast<std::uint8_t>((action.deepPress & 0x0F) | ((action.deepRelease & 0x0F) << 4));
    }
    return bytes;
}

OkmcConfig ProfileMemory::decodeOkmcEntry(const std::span<const std::uint8_t, OKMC_ENTRY_SIZE> bytes) noexcept {
    OkmcConfig config;
    const std::uint32_t travel = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                 (static_cast<std::uint32_t>(bytes[2]) << 16);
    config.shallowActTenths   = static_cast<std::uint8_t>(travel & 0x3F);
    config.shallowDeactTenths = static_cast<std::uint8_t>((travel >> 6) & 0x3F);
    config.deepActTenths      = static_cast<std::uint8_t>((travel >> 12) & 0x3F);
    config.deepDeactTenths    = static_cast<std::uint8_t>((travel >> 18) & 0x3F);
    for (std::size_t i = 0; i < 4; ++i) {
        config.keycodes[i] = static_cast<std::uint16_t>(bytes[3 + i * 2] | (bytes[4 + i * 2] << 8));
        OkmcAction& action = config.actions[i];
        action.shallowPress   = bytes[11 + i * 2] & 0x0F;
        action.shallowRelease = bytes[11 + i * 2] >> 4;
        action.deepPress      = bytes[12 + i * 2] & 0x0F;
        action.deepRelease    = bytes[12 + i * 2] >> 4;
    }
    return config;
}

std::optional<OkmcConfig> ProfileMemory::decodeOkmc(std::span<const std::uint8_t> profile, const std::size_t index) noexcept {
    if (index >= OKMC_COUNT) return std::nullopt;
    const std::size_t offset = OKMC_OFFSET + index * OKMC_ENTRY_SIZE;
    if (profile.size() < offset + OKMC_ENTRY_SIZE) return std::nullopt;
    return decodeOkmcEntry(profile.subspan(offset).first<OKMC_ENTRY_SIZE>());
}

bool ProfileMemory::encodeOkmc(std::span<std::uint8_t> profile, const std::size_t index, const OkmcConfig& config) noexcept {
    if (index >= OKMC_COUNT) return false;
    const std::size_t offset = OKMC_OFFSET + index * OKMC_ENTRY_SIZE;
    if (profile.size() < offset + OKMC_ENTRY_SIZE) return false;
    const auto bytes = encodeOkmcEntry(config);
    std::copy(bytes.begin(), bytes.end(), profile.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
}

KeyTravelConfig ProfileMemory::resolveKeyTravel(const GlobalTravelConfig& global, const KeyTravelConfig& key) noexcept {
    KeyTravelConfig effective = key;
    effective.mode            = key.mode == GlobalTravelConfig::MODE_GLOBAL ? global.mode : key.mode;
    effective.actuationTenths = key.actuationTenths == 0 ? global.actuationTenths : key.actuationTenths;
    effective.pressSensTenths = key.pressSensTenths == 0 ? global.pressSensTenths : key.pressSensTenths;
    if (key.releaseSensTenths != 0) {
        effective.releaseSensTenths = key.releaseSensTenths;
    } else {
        effective.releaseSensTenths = global.releaseSensTenths == 0 ? global.pressSensTenths : global.releaseSensTenths;
    }
    return effective;
}

} // namespace kbd
