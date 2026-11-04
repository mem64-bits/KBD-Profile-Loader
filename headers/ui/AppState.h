#pragma once

#include "kbd/GameController.h"
#include "kbd/IDeviceObserver.h"
#include "kbd/ProfileMemory.h"
#include "ui/ActuationModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ui {

    // The Keychron K2 HE keeps three profiles ("slots") in flash.
    inline constexpr std::size_t SLOT_COUNT = 3;

    // One of the keyboard's three slots and the profile file assigned to it.
    struct SlotInfo {
        bool                             hasProfile{false};
        std::string                      name;           // profile name, or "Slot N" while no file is assigned
        std::string                      fileName;       // JSON file backing the slot
        std::optional<ActuationSettings> storedSettings; // global settings decoded from the profile file
        std::optional<KeyMatrix>         storedKeys;     // per-key settings decoded from the profile file

        bool operator==(const SlotInfo&) const = default;
    };

    // What a profile contains besides travel settings.
    struct ProfileExtras {
        std::size_t socdPairs{0};   // SOCD / "Snap Tap" pairs
        std::size_t gamepadKeys{0}; // keys mapped to a game-controller axis or button
        std::size_t toggleKeys{0};
        std::size_t dynamicKeys{0}; // dynamic keystrokes (DKS)

        bool operator==(const ProfileExtras&) const = default;
    };

    // A profile file in the profiles folder (there can be any number of them).
    struct LibraryProfileInfo {
        std::string                      name;           // e.g. "Tekken 8 Mishima"
        std::string                      fileName;       // e.g. "HEProfile-Tekken 8.json"
        std::optional<std::uint8_t>      assignedSlot;   // nullopt = not assigned to a slot ("library only")
        std::optional<ActuationSettings> actuation;      // global settings decoded from the file
        std::size_t                      keyOverrides{0}; // keys with their own settings in the file
        ProfileExtras                    extras;

        bool operator==(const LibraryProfileInfo&) const = default;
    };

    // A key's advanced mode as stored in a slot (key_config adv_mode / adv_mode_data): kbd::AdvancedMode::*
    // with the gamepad target (kbd::GamepadTarget) or the dynamic-keystroke entry as data.
    struct KeyAdvanced {
        std::uint8_t mode{kbd::AdvancedMode::NONE};
        std::uint8_t data{0};
        bool operator==(const KeyAdvanced&) const = default;
    };

    // What a slot holds besides travel settings, read back from the keyboard: every key's advanced mode,
    // the 20 dynamic keystrokes and the 20 SOCD pairs.
    struct SlotDetails {
        std::array<KeyAdvanced, MATRIX_ROWS * MATRIX_COLS>                keys{};
        std::array<kbd::OkmcConfig, kbd::ProfileMemory::OKMC_COUNT>       okmc{};
        std::array<kbd::SocdPair, kbd::ProfileMemory::SOCD_COUNT>         socd{};

        [[nodiscard]] KeyAdvanced at(const KeyPosition& key) const noexcept { return keys[KeyMatrix::index(key)]; }
        void set(const KeyPosition& key, const KeyAdvanced& value) noexcept { keys[KeyMatrix::index(key)] = value; }

        // The dynamic-keystroke entry `key` already uses, else the first entry no key points at.
        [[nodiscard]] std::optional<std::uint8_t> okmcIndexFor(const KeyPosition& key) const noexcept;
        // SOCD pairs in use, in entry order.
        [[nodiscard]] std::vector<kbd::SocdPair> socdPairs() const;

        // From an analog_matrix_profile_t dump (at least up to the SOCD table).
        [[nodiscard]] static SlotDetails fromMemory(std::span<const std::uint8_t> memory);

        bool operator==(const SlotDetails&) const = default;
    };

    // One change to a key's advanced mode.
    struct AdvancedKeyChange {
        enum class Kind : std::uint8_t {
            Clear,            // a plain key again
            Gamepad,          // `index` = kbd::GamepadTarget
            Toggle,
            DynamicKeystroke, // `okmc` stored as entry `index`
        };

        KeyPosition     key;
        Kind            kind{Kind::Clear};
        std::uint8_t    index{0};
        kbd::OkmcConfig okmc{};

        bool operator==(const AdvancedKeyChange&) const = default;
    };

    // The keyboard-wide game-controller settings (not part of a slot).
    struct ControllerSettings {
        std::optional<std::uint8_t>        mode;  // kbd::ControllerMode bits
        std::optional<kbd::ResponseCurve>  curve;
        // Which controller interface the PC has enumerated (true XInput, false HID joystick), read from the HID
        // device list: what is running, as opposed to `mode`, what the keyboard has stored for its next start.
        std::optional<bool>                enumeratedXInput;
        // The two disagree: the keyboard runs the old mode until it is enumerated again (cable replug, or a USB
        // restart from the PC). Derived by withEnumeration(); false while either side is unknown.
        bool                               replugNeeded{false};

        [[nodiscard]] ControllerSettings withEnumeration(const std::optional<bool> enumerated) const {
            ControllerSettings copy = *this;
            copy.enumeratedXInput = enumerated;
            copy.replugNeeded = copy.mode && enumerated &&
                                (((*copy.mode & kbd::ControllerMode::XINPUT) != 0) != *enumerated);
            return copy;
        }
        bool operator==(const ControllerSettings&) const = default;
    };

    // How far one key is pressed right now (AMC_GET_REALTIME_TRAVEL).
    struct LiveKeyTravel {
        KeyPosition  key;
        std::uint8_t travelTenths{0}; // 0 - 40
        std::uint8_t travelRaw{0};    // 0 - 240 (1/60 mm)
        bool         pressed{false};
        bool operator==(const LiveKeyTravel&) const = default;
    };

    struct DeviceInfo {
        bool               connected{false};
        kbd::TransportMode   transport{kbd::TransportMode::Disconnected};
        std::string          path;
        kbd::KeyboardVariant variant{kbd::KeyboardVariant::Unknown}; // ANSI / ISO / JIS, from the USB product id

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
        std::vector<LibraryProfileInfo>                           library;        // every profile file, in cycling order
        std::array<std::optional<ActuationSettings>, SLOT_COUNT>  actuation;      // last value read back / applied
        std::array<std::optional<KeyMatrix>, SLOT_COUNT>          keyMatrix;      // per-key values read back / applied
        std::array<std::optional<SlotDetails>, SLOT_COUNT>        details;        // advanced modes, DKS, SOCD read back
        ControllerSettings                                        controller;     // gamepad mode + response curve read back
        bool                                                      busy{false};    // a hardware command is running
        StatusMessage                                             status;

        bool operator==(const AppSnapshot&) const = default;
    };

    [[nodiscard]] const char* transportName(kbd::TransportMode mode) noexcept; // "Wired USB-C" / "Disconnected"
    [[nodiscard]] const char* variantName(kbd::KeyboardVariant variant) noexcept; // "ISO", or "" when unknown
    [[nodiscard]] std::string slotLabel(std::size_t slot);                     // 0 -> "Slot 1"

} // namespace ui
