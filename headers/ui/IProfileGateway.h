#pragma once

#include "ui/AppState.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui {

    struct CommandResult {
        bool        ok{false};
        std::string message;
    };

    // The port through which the ui layer reaches the profile / keyboard back end (the adapter
    // ProfileServiceGateway implements it on top of kbd::ProfileService). Keeping it an interface lets
    // the controller be tested with a fake and keeps ui code free of HID details.
    //
    // NOT thread-safe by design: ProfileController is the only caller and runs every method on its single
    // worker thread, so profile data and HID transactions can never interleave.
    class IProfileGateway {
    public:
        virtual ~IProfileGateway() = default;

        // --- queries (no keyboard traffic) ---
        [[nodiscard]] virtual DeviceInfo describeDevice() const = 0;
        [[nodiscard]] virtual std::array<SlotInfo, SLOT_COUNT> describeSlots() const = 0;

        // --- keyboard round-trips (blocking) ---
        [[nodiscard]] virtual int queryActiveSlot() = 0; // -1 when unknown / not connected
        virtual CommandResult activateSlot(std::uint8_t slot) = 0;
        virtual CommandResult cycleProfile(bool forward) = 0;
        virtual CommandResult applyActuation(std::uint8_t slot, const ActuationSettings& settings) = 0;

        // Reads the slot's global actuation settings back from the keyboard. `expected` is what the caller
        // believes is stored there; it is sent as the (normally ignored) payload of the read request so that
        // a firmware which wrongly interpreted the request as a write would rewrite the same values.
        [[nodiscard]] virtual std::optional<ActuationSettings> readActuation(std::uint8_t slot,
                                                                             const std::optional<ActuationSettings>& expected) = 0;

        // Gives `keys` of `slot` their own travel settings (one AMC_SET_TRAVEL with key masks). The firmware
        // applies per-key values at once when `slot` is active. A default KeyActuation hands the keys back to
        // the slot's global settings.
        virtual CommandResult applyKeyActuation(std::uint8_t slot, const std::vector<KeyPosition>& keys,
                                                const KeyActuation& value) = 0;

        // Reads the slot's per-key settings back from the keyboard; nullopt without real hardware.
        [[nodiscard]] virtual std::optional<KeyMatrix> readKeyMatrix(std::uint8_t slot) = 0;

        virtual CommandResult saveSlot(std::uint8_t slot) = 0;

        // Writes the travel settings of the profile file assigned to `slot` (global + every key) into the
        // keyboard and commits them to flash. The firmware has no raw-profile write, so this replays the setters.
        virtual CommandResult flashSlotProfile(std::uint8_t slot) = 0;

        // --- events produced by the file / device watchers, forwarded to the profile service ---
        virtual void handleFileCreated(const std::filesystem::path& path) = 0;
        virtual void handleFileDeleted(const std::filesystem::path& path) = 0;
        virtual void handleDeviceConnected(kbd::TransportMode mode, const std::string& devicePath) = 0;
        virtual void handleDeviceDisconnected() = 0;

        // --- profile library (every file in the profiles folder) ---
        [[nodiscard]] virtual std::vector<LibraryProfileInfo> describeLibrary() const = 0;

        // Moves the profile at `fromIndex` so that it ends up at `toIndex` (the cycling order).
        virtual CommandResult reorderLibrary(std::size_t fromIndex, std::size_t toIndex) = 0;

        // Makes `fileName` the profile of `targetSlot` (saved in the file's "current" field); any other file
        // that claimed the slot loses it. With writeNow the profile is also written to the keyboard.
        virtual CommandResult assignProfileToSlot(const std::string& fileName, std::uint8_t targetSlot, bool writeNow) = 0;

        // Removes the slot assignment from the file(s) that claim `slot`.
        virtual CommandResult clearSlot(std::uint8_t slot) = 0;

        // --- advanced keys, SOCD, game controller (keyboard round-trips) ---

        // The whole stored profile of `slot` decoded (advanced key modes, dynamic keystrokes, SOCD pairs).
        [[nodiscard]] virtual std::optional<SlotDetails> readSlotDetails(std::uint8_t slot) = 0;
        virtual CommandResult applyAdvancedKey(std::uint8_t slot, const AdvancedKeyChange& change) = 0;
        // SOCD entry `index` (0 - 19); a pair with type 0 clears it.
        virtual CommandResult applySocd(std::uint8_t slot, std::size_t index, const kbd::SocdPair& pair) = 0;
        [[nodiscard]] virtual std::optional<ControllerSettings> readControllerSettings() = 0;
        virtual CommandResult applyControllerMode(std::uint8_t mode) = 0;
        // KeyboardDevice::enumeratedAsXInput(): the controller interface the PC enumerated (nullopt: unknown).
        [[nodiscard]] virtual std::optional<bool> enumeratedXInput() { return std::nullopt; }
        virtual CommandResult applyCurve(const kbd::ResponseCurve& curve) = 0;
        // How far `key` is pressed right now; nullopt without hardware or on a bad reply.
        [[nodiscard]] virtual std::optional<LiveKeyTravel> readLiveTravel(const KeyPosition& key) = 0;
    };

} // namespace ui
