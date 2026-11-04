#include "ui/AppState.h"

namespace ui {

const char* transportName(const kbd::TransportMode mode) noexcept {
    switch (mode) {
        case kbd::TransportMode::Wired:        return "Wired USB-C";
        case kbd::TransportMode::Disconnected: break;
    }
    return "Disconnected";
}

const char* variantName(const kbd::KeyboardVariant variant) noexcept {
    switch (variant) {
        case kbd::KeyboardVariant::Ansi:    return "ANSI";
        case kbd::KeyboardVariant::Iso:     return "ISO";
        case kbd::KeyboardVariant::Jis:     return "JIS";
        case kbd::KeyboardVariant::Unknown: break;
    }
    return "";
}

std::optional<std::uint8_t> SlotDetails::okmcIndexFor(const KeyPosition& key) const noexcept {
    const KeyAdvanced own = at(key);
    if (own.mode == kbd::AdvancedMode::DKS && own.data < okmc.size()) return own.data;

    std::array<bool, kbd::ProfileMemory::OKMC_COUNT> used{};
    for (const KeyAdvanced& other : keys) {
        if (other.mode == kbd::AdvancedMode::DKS && other.data < used.size()) used[other.data] = true;
    }
    for (std::uint8_t i = 0; i < used.size(); ++i) {
        if (!used[i]) return i;
    }
    return std::nullopt;
}

std::vector<kbd::SocdPair> SlotDetails::socdPairs() const {
    std::vector<kbd::SocdPair> pairs;
    for (const kbd::SocdPair& pair : socd) {
        if (pair.isSet()) pairs.push_back(pair);
    }
    return pairs;
}

SlotDetails SlotDetails::fromMemory(const std::span<const std::uint8_t> memory) {
    SlotDetails details;
    for (std::uint8_t row = 0; row < MATRIX_ROWS; ++row) {
        for (std::uint8_t col = 0; col < MATRIX_COLS; ++col) {
            if (const auto key = kbd::ProfileMemory::decodeKeyTravel(memory, row, col)) {
                details.set({row, col}, {key->advancedMode, key->advancedData});
            }
        }
    }
    for (std::size_t i = 0; i < details.okmc.size(); ++i) {
        details.okmc[i] = kbd::ProfileMemory::decodeOkmc(memory, i).value_or(kbd::OkmcConfig{});
    }
    for (std::size_t i = 0; i < details.socd.size(); ++i) {
        details.socd[i] = kbd::ProfileMemory::decodeSocd(memory, i).value_or(kbd::SocdPair{});
    }
    return details;
}

std::string slotLabel(const std::size_t slot) {
    return "Slot " + std::to_string(slot + 1);
}

} // namespace ui
