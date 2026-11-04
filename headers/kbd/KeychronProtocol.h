#pragma once

#include <vector>
#include <array>
#include <span>
#include <cstdint>
#include <string>
#include <nlohmann/json_fwd.hpp>
#include <optional>

#include "kbd/GameController.h"
#include "kbd/ProfileMemory.h"

namespace kbd {

class KeychronProtocol {
public:
    static constexpr size_t RAW_PACKET_SIZE       = 32;
    static constexpr size_t PROFILE_MEMORY_SIZE   = 860;
    static constexpr size_t MAX_CHUNK_PAYLOAD     = 26;
    static constexpr size_t TOTAL_CHUNKS          = 34; // 33 * 26B + 1 * 2B
    static constexpr size_t MATRIX_ROWS           = 6;
    static constexpr size_t MATRIX_COLS           = 16;
    static constexpr size_t MAX_PROFILE_NAME_LENGTH = 28; // profile_set_name() rejects longer names

    // AMC_SET_TRAVEL layout (analog_matrix.c): [2] slot, [3] mode, [4] actuation, [5] press sens.,
    // [6] release sens., [7] 1 = whole keyboard, [8..25] per-key masks, 3 bytes per row (little-endian).
    static constexpr size_t  KEY_MASK_OFFSET        = 8;
    static constexpr size_t  KEY_MASK_BYTES_PER_ROW = 3;
    static constexpr uint8_t TRAVEL_MODE_GLOBAL     = 0; // per key only: use the profile's global value
    static constexpr uint8_t TRAVEL_MODE_REGULAR    = 1;
    static constexpr uint8_t TRAVEL_MODE_RAPID      = 2;

    // AMC_SET_ADVANCE_MODE [3] (profile.c: ADV_MODE_*), [4] row, [5] col, [6] index
    static constexpr uint8_t ADV_MODE_CLEAR           = 0;
    static constexpr uint8_t ADV_MODE_OKMC            = 1; // dynamic keystrokes; needs the OKMC data as well
    static constexpr uint8_t ADV_MODE_GAME_CONTROLLER = 2; // index = joystick axis / button
    static constexpr uint8_t ADV_MODE_TOGGLE          = 3;

    // Command groups (keychron_raw_hid.h). 0xA8 is lighting (KC_KEYCHRON_RGB), left to OpenRGB / the Launcher.
    static constexpr uint8_t CMD_ANALOG_MATRIX          = 0xA9; // AMC Subsystem

    // Official Keychron AMC (Analog Matrix Controller) Subcommands
    static constexpr uint8_t AMC_GET_VERSION            = 0x01;
    static constexpr uint8_t AMC_GET_PROFILES_INFO      = 0x10; // Query active slot & count
    static constexpr uint8_t AMC_SELECT_PROFILE         = 0x11; // Fast slot select
    static constexpr uint8_t AMC_GET_PROFILE_RAW        = 0x12; // Reads up to 26 bytes of a stored profile
    static constexpr uint8_t AMC_SET_PROFILE_NAME       = 0x13; // Set ASCII name string on MCU
    static constexpr uint8_t AMC_SET_TRAVEL             = 0x14; // Single-packet fast actuation/RT tuning
    static constexpr uint8_t AMC_SET_ADVANCE_MODE       = 0x15; // Per-key advanced mode (DKS, gamepad, toggle)
    static constexpr uint8_t AMC_SET_SOCD               = 0x16; // One SOCD ("Snap Tap") pair
    static constexpr uint8_t AMC_RESET_PROFILE          = 0x1E; // Factory values for one slot
    static constexpr uint8_t AMC_SAVE_PROFILE           = 0x1F; // Commit RAM staging buffer to SPI Flash
    static constexpr uint8_t AMC_GET_CURVE              = 0x20; // Stick response curve (4 points)
    static constexpr uint8_t AMC_SET_CURVE              = 0x21; // ... written to EEPROM at once
    static constexpr uint8_t AMC_GET_GAME_CONTROLLER_MODE = 0x22; // XInput / HID joystick, "keys also type"
    static constexpr uint8_t AMC_SET_GAME_CONTROLLER_MODE = 0x23; // ... written to EEPROM at once
    static constexpr uint8_t AMC_GET_REALTIME_TRAVEL    = 0x30; // How far one key is pressed right now
    static constexpr uint8_t AMC_CALIBRATE              = 0x40;
    static constexpr uint8_t AMC_GET_CALIBRATE_STATE    = 0x41;
    static constexpr uint8_t AMC_GET_CALIBRATED_VALUE   = 0x42;

    // 1. Slot Selection & Status Query
    [[nodiscard]] static std::vector<uint8_t> createSlotSelectPacket(uint8_t slotIndex);
    [[nodiscard]] static std::vector<uint8_t> createStatusQueryPacket();

    // 2. Commit Staging RAM to Board Flash/EEPROM (0x1F)
    [[nodiscard]] static std::vector<uint8_t> createSaveProfilePacket(uint8_t slotIndex);

    // 3. Whole-keyboard actuation & Rapid Trigger (0x14 with "entire keyboard" set)
    // actPointTenths: 3 = 0.3mm, 14 = 1.4mm
    // pressSensTenths: 1 = 0.10mm, 2 = 0.20mm
    // releaseSensTenths: 1 = 0.10mm, 2 = 0.20mm
    // With entireKeyboard = false no key is selected; use createKeyTravelPacket for per-key settings.
    [[nodiscard]] static std::vector<uint8_t> createFastTravelPacket(
        uint8_t slotIndex,
        bool rapidTrigger,
        uint8_t actPointTenths,
        uint8_t pressSensTenths,
        uint8_t releaseSensTenths,
        bool entireKeyboard = true
    );

    // 3b. Per-key actuation (0x14 with key masks). rowMasks[row] bit c selects matrix column c.
    // mode: TRAVEL_MODE_GLOBAL / _REGULAR / _RAPID; distances 0 = "use the global value".
    [[nodiscard]] static std::vector<uint8_t> createKeyTravelPacket(
        uint8_t slotIndex,
        uint8_t mode,
        uint8_t actPointTenths,
        uint8_t pressSensTenths,
        uint8_t releaseSensTenths,
        const std::array<uint32_t, MATRIX_ROWS>& rowMasks
    );

    // 3c. Advanced mode of one key (0x15): ADV_MODE_CLEAR / _GAME_CONTROLLER / _TOGGLE (OKMC: see below).
    [[nodiscard]] static std::vector<uint8_t> createAdvanceModePacket(
        uint8_t slotIndex, uint8_t advMode, uint8_t row, uint8_t col, uint8_t index
    );

    // 3c'. A dynamic keystroke (0x15 with ADV_MODE_OKMC): stores `config` as OKMC entry `index` (0 - 19) of the
    // slot and points the key at it. [7..10] travel points, [11..18] keycodes (LE), [19..26] actions.
    [[nodiscard]] static std::vector<uint8_t> createOkmcPacket(
        uint8_t slotIndex, uint8_t row, uint8_t col, uint8_t index, const OkmcConfig& config
    );

    // 3d. SOCD pair `index` (0 - 19) of a slot (0x16); type 0 clears the entry.
    [[nodiscard]] static std::vector<uint8_t> createSocdPacket(
        uint8_t slotIndex, uint8_t index, uint8_t row1, uint8_t col1, uint8_t row2, uint8_t col2, uint8_t type
    );

    // 4. Set Hardware Profile Name on MCU (0x13); names longer than 28 characters are cut.
    [[nodiscard]] static std::vector<uint8_t> createSetProfileNamePacket(
        uint8_t slotIndex,
        const std::string& name
    );

    // 5. Reading a stored profile (0x12): `size` (at most 26) bytes from `offset`; the reply holds them at [6..].
    [[nodiscard]] static std::vector<uint8_t> createProfileReadPacket(uint8_t slotIndex, uint16_t offset, uint8_t size);

    // 5b. 860-byte chunking (0x12). NOTE: the firmware treats 0x12 as a read (AMC_GET_PROFILE_RAW) and has no
    // raw-write command, so these packets do not change the keyboard. Kept for the chunking maths and tests;
    // a profile is written by replaying the setters (see ProfileServiceGateway::writeProfile).
    [[nodiscard]] static std::vector<uint8_t> createMemoryChunkPacket(
        uint8_t slotIndex,
        uint16_t offset,
        std::span<const uint8_t> chunkData
    );
    [[nodiscard]] static std::vector<std::vector<uint8_t>> generateMemoryStreamPackets(
        uint8_t slotIndex,
        std::span<const uint8_t, PROFILE_MEMORY_SIZE> memoryBuffer
    );
    // A profile file as the 860-byte analog_matrix_profile_t: Launcher JSON ("global" + "keyMatrix"), or a
    // raw dump ("memory" array / "raw_hex"). Throws std::runtime_error when the file has none of them.
    [[nodiscard]] static std::array<uint8_t, PROFILE_MEMORY_SIZE> extractMemoryBuffer(
        const nlohmann::json& jsonConfig
    );

    // 6. Live key travel (0x30) of one key. Reply: [2] status (0 = ok), [3] row, [4] col, [5] travel in 0.1 mm,
    //    [6] travel in 1/60 mm, [7..8] raw sensor value, [9..10] calibrated zero, [11..12] calibrated full
    //    travel, [13] 0x11 pressed / 0x10 released.
    [[nodiscard]] static std::vector<uint8_t> createRealtimeTravelPacket(uint8_t row, uint8_t col);

    struct RealtimeTravel {
        uint8_t  row{0};
        uint8_t  col{0};
        uint8_t  travelTenths{0};  // 0 - 40 (4.0 mm)
        uint8_t  travelRaw{0};     // 0 - 240, TRAVEL_SCALE = 6 steps per 0.1 mm
        uint16_t sensorValue{0};
        uint16_t zeroValue{0};
        uint16_t fullValue{0};
        bool     pressed{false};
        bool operator==(const RealtimeTravel&) const noexcept = default;
    };
    // nullopt unless `reply` is a successful AMC_GET_REALTIME_TRAVEL answer for (row, col).
    [[nodiscard]] static std::optional<RealtimeTravel> parseRealtimeTravel(std::span<const uint8_t> reply, uint8_t row, uint8_t col);

    // 7. Game controller: response curve (0x20 / 0x21) and mode (0x22 / 0x23). Both setters write the keyboard's
    //    EEPROM immediately, so send them on an explicit "apply", never while dragging.
    [[nodiscard]] static std::vector<uint8_t> createCurveReadPacket();
    [[nodiscard]] static std::vector<uint8_t> createCurvePacket(const ResponseCurve& curve);
    // Reply of 0x20: the 4 points at [2..9] (no status byte).
    [[nodiscard]] static std::optional<ResponseCurve> parseCurve(std::span<const uint8_t> reply);
    [[nodiscard]] static std::vector<uint8_t> createControllerModeReadPacket();
    [[nodiscard]] static std::vector<uint8_t> createControllerModePacket(uint8_t mode);
    // Reply of 0x22: [2] status, [3] mode bits.
    [[nodiscard]] static std::optional<uint8_t> parseControllerMode(std::span<const uint8_t> reply);
};

} // namespace kbd