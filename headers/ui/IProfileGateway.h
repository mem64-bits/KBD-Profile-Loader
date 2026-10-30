#pragma once

#include "ui/AppState.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

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

        virtual CommandResult saveSlot(std::uint8_t slot) = 0;
        virtual CommandResult flashSlotProfile(std::uint8_t slot) = 0;

        // --- events produced by the file / device watchers, forwarded to the profile service ---
        virtual void handleFileCreated(const std::filesystem::path& path) = 0;
        virtual void handleFileDeleted(const std::filesystem::path& path) = 0;
        virtual void handleDeviceConnected(kbd::TransportMode mode, const std::string& devicePath) = 0;
        virtual void handleDeviceDisconnected() = 0;
    };

} // namespace ui
