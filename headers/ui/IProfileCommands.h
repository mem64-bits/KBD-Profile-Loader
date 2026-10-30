#pragma once

#include "ui/ActuationModel.h"

#include <cstdint>

namespace ui {

    // What the views, the tray and the hotkey / IPC handlers may ask the keyboard to do.
    // Every call is thread-safe and returns immediately: the work runs on a background thread and
    // its progress and outcome show up in the AppModel (busy flag + status message).
    class IProfileCommands {
    public:
        virtual ~IProfileCommands() = default;

        virtual void activateSlot(std::uint8_t slot) = 0;
        virtual void cycleProfile(bool forward) = 0;

        // Whole-keyboard actuation / Rapid Trigger for `slot`. Values are clamped to what the firmware
        // accepts. With saveToFlash the slot is committed to the keyboard's flash afterwards.
        virtual void applyActuation(std::uint8_t slot, const ActuationSettings& settings, bool saveToFlash) = 0;

        // Reads the slot's current settings back from the keyboard into the AppModel.
        virtual void readActuation(std::uint8_t slot) = 0;

        // Streams the profile file assigned to `slot` into the keyboard and commits it.
        virtual void flashSlot(std::uint8_t slot) = 0;

        // Re-reads profiles, device state and the active slot.
        virtual void refresh() = 0;
    };

} // namespace ui
