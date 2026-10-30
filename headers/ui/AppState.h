#pragma once

#include "kbd/IDeviceObserver.h"
#include "ui/ActuationModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ui {

    // The Keychron K2 HE keeps three profiles ("slots") in flash.
    inline constexpr std::size_t SLOT_COUNT = 3;

    struct SlotInfo {
        bool                             hasProfile{false};
        std::string                      name;           // profile name, or "Slot N" while the slot is empty
        std::string                      fileName;       // JSON file backing the slot
        std::optional<ActuationSettings> storedSettings; // decoded from the profile file's memory dump

        bool operator==(const SlotInfo&) const = default;
    };

    struct DeviceInfo {
        bool               connected{false};
        kbd::TransportMode transport{kbd::TransportMode::Disconnected};
        std::string        path;

        bool operator==(const DeviceInfo&) const = default;
    };

    enum class StatusSeverity {
        None,
        Info,
        Success,
        Warning,
        Error
    };

    struct StatusMessage {
        StatusSeverity severity{StatusSeverity::None};
        std::string    text;
        std::uint64_t  serial{0}; // changes with every message, even when the text repeats

        bool operator==(const StatusMessage&) const = default;
    };

    // Everything the views and the tray need to draw themselves. Produced by ProfileController
    // on its worker thread, consumed by the UI / tray threads as an immutable copy.
    struct AppSnapshot {
        DeviceInfo                                                device;
        int                                                       activeSlot{-1}; // -1 = unknown
        std::array<SlotInfo, SLOT_COUNT>                          slots;
        std::array<std::optional<ActuationSettings>, SLOT_COUNT>  actuation;     // last value read back / applied
        bool                                                      busy{false};    // a hardware command is running
        StatusMessage                                             status;

        bool operator==(const AppSnapshot&) const = default;
    };

    [[nodiscard]] const char* transportName(kbd::TransportMode mode) noexcept; // "Wired USB-C", "2.4G dongle", ...
    [[nodiscard]] std::string slotLabel(std::size_t slot);                     // 0 -> "Slot 1"

} // namespace ui
