#pragma once

#include "ui/ActuationModel.h"
#include "ui/AppState.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

        // Gives `keys` of `slot` their own settings (a default KeyActuation resets them to the slot's values),
        // then reads the matrix back to verify. With saveToFlash the slot is committed afterwards.
        virtual void applyKeyActuation(std::uint8_t slot, std::vector<KeyPosition> keys, const KeyActuation& value,
                                       bool saveToFlash) = 0;

        // Reads the slot's global and per-key settings back from the keyboard into the AppModel.
        virtual void readActuation(std::uint8_t slot) = 0;

        // Writes the travel settings of the profile file assigned to `slot` into the keyboard and commits them.
        virtual void flashSlot(std::uint8_t slot) = 0;

        // Re-reads profiles, device state and the active slot.
        virtual void refresh() = 0;

        // Moves a library profile from one position to another (the order the hotkeys cycle through).
        virtual void reorderLibrary(std::size_t fromIndex, std::size_t toIndex) = 0;

        // Assigns a profile file to a slot (0, 1, 2); with writeNow it is also written to the keyboard.
        virtual void assignProfileToSlot(const std::string& fileName, std::uint8_t targetSlot, bool writeNow) = 0;

        // Removes the profile assignment from a slot (its file stays in the library).
        virtual void clearSlot(std::uint8_t slot) = 0;

        // --- advanced keys, SOCD, game controller ---

        // Reads the slot's advanced key modes, dynamic keystrokes and SOCD pairs into AppSnapshot::details.
        virtual void readSlotDetails(std::uint8_t slot) = 0;
        // Changes the advanced mode of keys (gamepad target, toggle, dynamic keystroke or back to a plain key),
        // then reads the slot back to verify. With saveToFlash the slot is committed afterwards.
        virtual void applyAdvancedKeys(std::uint8_t slot, std::vector<AdvancedKeyChange> changes, bool saveToFlash) = 0;
        // Makes `pairs` the slot's SOCD pairs (all 20 entries are written; the rest are cleared).
        virtual void applySocdPairs(std::uint8_t slot, std::vector<kbd::SocdPair> pairs, bool saveToFlash) = 0;
        // Reads the keyboard-wide controller mode and response curve into AppSnapshot::controller.
        virtual void readControllerSettings() = 0;
        // Both are stored in the keyboard's EEPROM at once (no separate save).
        virtual void applyControllerMode(std::uint8_t mode) = 0;
        // Automatic gamepad mode: when a slot becomes active, turn XInput on if it has gamepad keys and off if it
        // has none (on by default).
        virtual void setAutoControllerMode(bool enabled) { (void)enabled; }
        [[nodiscard]] virtual bool autoControllerMode() const noexcept { return false; }
        virtual void applyCurve(const kbd::ResponseCurve& curve) = 0;

        // Keys whose live travel the views want to see (empty = stop polling). While the window is open the
        // keys are read about 60 times a second; liveTravel() returns the latest readings.
        virtual void watchLiveTravel(std::vector<KeyPosition> keys) = 0;
        [[nodiscard]] virtual std::vector<LiveKeyTravel> liveTravel() const = 0;
    };

} // namespace ui
