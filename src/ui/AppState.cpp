#include "ui/AppState.h"

namespace ui {

const char* transportName(const kbd::TransportMode mode) noexcept {
    switch (mode) {
        case kbd::TransportMode::Wired:          return "Wired USB-C";
        case kbd::TransportMode::WirelessDongle: return "2.4G dongle";
        case kbd::TransportMode::Disconnected:   break;
    }
    return "Disconnected";
}

std::string slotLabel(const std::size_t slot) {
    return "Slot " + std::to_string(slot + 1);
}

} // namespace ui
