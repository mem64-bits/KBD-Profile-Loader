#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace kbd {

    // One `analog_key_config_t` (Keychron/qmk_firmware: keyboards/keychron/common/analog_matrix).
    // The profile's global settings and every per-key entry use this same packed 4-byte layout:
    //
    //   byte 0: bits 0-1 mode            bits 2-7 actuation point (0.1 mm)
    //   byte 1: bits 0-5 press sens.     bits 6-7 release sens. (low 2 bits)
    //   byte 2: bits 0-3 release sens. (high 4 bits)   bits 4-7 advanced mode
    //   byte 3: advanced-mode data
    //
    // For a per-key entry 0 means "use the global value" in every field (mode, actuation, sensitivities).
    struct GlobalTravelConfig {
        static constexpr std::uint8_t MODE_GLOBAL  = 0; // key follows the profile's global mode
        static constexpr std::uint8_t MODE_REGULAR = 1; // static actuation point
        static constexpr std::uint8_t MODE_RAPID   = 2; // Rapid Trigger

        std::uint8_t mode{MODE_REGULAR};
        std::uint8_t actuationTenths{0};   // 0.1 mm units; 0 = "use the firmware default" / the global value
        std::uint8_t pressSensTenths{0};   // Rapid Trigger press sensitivity, 0.1 mm units
        std::uint8_t releaseSensTenths{0}; // Rapid Trigger release sensitivity; 0 = same as press
        std::uint8_t advancedMode{0};
        std::uint8_t advancedData{0};

        [[nodiscard]] bool isRapidTrigger() const noexcept;

        bool operator==(const GlobalTravelConfig&) const noexcept = default;
    };

    // The settings of one key, same layout as the global ones.
    using KeyTravelConfig = GlobalTravelConfig;

    // `adv_mode` of a key (analog_matrix_type.h, AKM_*): what the key does instead of a plain key press.
    // `advancedData` is then the dynamic-keystroke slot (DKS) or the joystick axis / button (gamepad).
    namespace AdvancedMode {
        inline constexpr std::uint8_t NONE    = 0;
        inline constexpr std::uint8_t DKS     = 3; // dynamic keystrokes (OKMC)
        inline constexpr std::uint8_t GAMEPAD = 4; // analog game-controller axis / button
        inline constexpr std::uint8_t TOGGLE  = 5;
    }

    // One `socd_config_t`: two keys (by matrix position) that cancel each other, and how ("Snap Tap").
    // Packed as 3 bytes: key 1 row (3 bits) | col (5 bits), key 2 the same, then the type.
    struct SocdPair {
        static constexpr std::uint8_t TYPE_NONE    = 0; // empty entry
        static constexpr std::uint8_t TYPE_MAX     = 6; // SOCD_PRI_NEUTRAL; SOCD_PRI_MAX = 7 is invalid

        std::uint8_t row1{0};
        std::uint8_t col1{0};
        std::uint8_t row2{0};
        std::uint8_t col2{0};
        std::uint8_t type{TYPE_NONE};

        [[nodiscard]] bool isSet() const noexcept { return type != TYPE_NONE; }
        bool operator==(const SocdPair&) const noexcept = default;
    };

    // What one of the (up to 4) keycodes of a dynamic keystroke does at each point of the key's travel
    // (okmc_action_t, 4 bits each). action_okmc.c runs the bits in order 0, 1, 2 with "release" on the even
    // bits and "press" on bit 1, hence the odd-looking values.
    struct OkmcAction {
        static constexpr std::uint8_t NONE    = 0b000;
        static constexpr std::uint8_t RELEASE = 0b001;
        static constexpr std::uint8_t PRESS   = 0b010;
        static constexpr std::uint8_t TAP     = 0b110; // press, then release
        static constexpr std::uint8_t RETAP   = 0b111; // release, press, release

        std::uint8_t shallowPress{NONE};   // the key goes down past the shallow point
        std::uint8_t shallowRelease{NONE}; // ... and comes back up past it
        std::uint8_t deepPress{NONE};      // the key goes down past the deep point
        std::uint8_t deepRelease{NONE};    // ... and comes back up past it

        bool operator==(const OkmcAction&) const noexcept = default;
    };

    // One `okmc_config_t` (19 bytes): a dynamic keystroke ("One Key Multiple Commands" in the Launcher).
    // A key in DKS mode sends up to 4 keycodes depending on how deep it is pressed. Distances in 0.1 mm.
    struct OkmcConfig {
        std::uint8_t                  shallowActTenths{0};
        std::uint8_t                  shallowDeactTenths{0};
        std::uint8_t                  deepActTenths{0};
        std::uint8_t                  deepDeactTenths{0};
        std::array<std::uint16_t, 4>  keycodes{}; // QMK basic keycodes / modifiers (0 = unused)
        std::array<OkmcAction, 4>     actions{};

        [[nodiscard]] bool isEmpty() const noexcept;
        bool operator==(const OkmcConfig&) const noexcept = default;
    };

    // Common QMK keycodes (= USB HID usage ids) used by the presets.
    namespace Keycode {
        inline constexpr std::uint16_t A          = 0x04; // A .. Z are 0x04 .. 0x1D
        inline constexpr std::uint16_t W          = 0x1A;
        inline constexpr std::uint16_t NUM_1      = 0x1E; // 1 .. 9, then 0 = 0x27
        inline constexpr std::uint16_t SPACE      = 0x2C;
        inline constexpr std::uint16_t TAB        = 0x2B;
        inline constexpr std::uint16_t CAPS_LOCK  = 0x39;
        inline constexpr std::uint16_t RIGHT      = 0x4F;
        inline constexpr std::uint16_t LEFT       = 0x50;
        inline constexpr std::uint16_t DOWN       = 0x51;
        inline constexpr std::uint16_t UP         = 0x52;
        inline constexpr std::uint16_t LEFT_CTRL  = 0xE0;
        inline constexpr std::uint16_t LEFT_SHIFT = 0xE1;
        inline constexpr std::uint16_t LEFT_ALT   = 0xE2;
    }

    // "Sprint on deep press": past `shallowTenths` the key sends `baseKeycode` (W walks); past `deepTenths` it
    // also holds `extraKeycode` (Shift = run) until it comes back up past the deep point (minus a little
    // hysteresis); letting go releases both. The firmware only re-arms the deep point after a full release.
    [[nodiscard]] OkmcConfig deepPressCombo(std::uint16_t baseKeycode, std::uint16_t extraKeycode,
                                            std::uint8_t shallowTenths, std::uint8_t deepTenths) noexcept;

    // Offsets inside `analog_matrix_profile_t`, the 860-byte profile the keyboard keeps per slot.
    // Checked against analog_matrix_type.h / analog_matrix_eeconfig.h (branch 2025q3) for the K2 HE's 6 x 16
    // matrix with OKMC_COUNT = SOCD_COUNT = 20 and PROFILE_NAME_LEN = 30:
    //
    //   offset   0  global                       4 bytes
    //   offset   4  key_config[6][16]       96 x 4 bytes
    //   offset 388  okmc[20]  (dynamic keys) 20 x 19 bytes
    //   offset 768  socd[20]                 20 x 3 bytes
    //   offset 828  name[30]
    //   offset 858  crc16
    class ProfileMemory {
    public:
        static constexpr std::size_t MATRIX_ROWS = 6;
        static constexpr std::size_t MATRIX_COLS = 16;
        static constexpr std::size_t KEY_COUNT   = MATRIX_ROWS * MATRIX_COLS;

        static constexpr std::size_t TRAVEL_CONFIG_SIZE   = 4;
        static constexpr std::size_t GLOBAL_TRAVEL_OFFSET = 0;
        static constexpr std::size_t GLOBAL_TRAVEL_SIZE   = TRAVEL_CONFIG_SIZE;
        static constexpr std::size_t KEY_CONFIG_OFFSET    = GLOBAL_TRAVEL_OFFSET + GLOBAL_TRAVEL_SIZE;
        static constexpr std::size_t KEY_CONFIG_SIZE      = KEY_COUNT * TRAVEL_CONFIG_SIZE;
        static constexpr std::size_t OKMC_COUNT           = 20;
        static constexpr std::size_t OKMC_ENTRY_SIZE      = 19; // 3 travel + 4 x uint16 keycodes + 4 x 2 actions
        static constexpr std::size_t OKMC_OFFSET          = KEY_CONFIG_OFFSET + KEY_CONFIG_SIZE;
        static constexpr std::size_t SOCD_COUNT           = 20;
        static constexpr std::size_t SOCD_ENTRY_SIZE      = 3;
        static constexpr std::size_t SOCD_OFFSET          = OKMC_OFFSET + OKMC_COUNT * OKMC_ENTRY_SIZE;
        static constexpr std::size_t NAME_OFFSET          = SOCD_OFFSET + SOCD_COUNT * SOCD_ENTRY_SIZE;
        static constexpr std::size_t NAME_SIZE            = 30;
        static constexpr std::size_t CRC_OFFSET           = NAME_OFFSET + NAME_SIZE;
        static constexpr std::size_t PROFILE_SIZE         = CRC_OFFSET + 2;

        // Byte offset of key (row, col) inside the profile.
        [[nodiscard]] static constexpr std::size_t keyConfigOffset(const std::size_t row, const std::size_t col) noexcept {
            return KEY_CONFIG_OFFSET + (row * MATRIX_COLS + col) * TRAVEL_CONFIG_SIZE;
        }

        // Needs at least GLOBAL_TRAVEL_SIZE bytes; extra bytes are ignored.
        [[nodiscard]] static std::optional<GlobalTravelConfig> decodeGlobalTravel(std::span<const std::uint8_t> bytes) noexcept;

        // Inverse of decodeGlobalTravel (values are masked to their field widths).
        [[nodiscard]] static std::array<std::uint8_t, GLOBAL_TRAVEL_SIZE> encodeGlobalTravel(const GlobalTravelConfig& config) noexcept;

        // Per-key entry of a whole profile dump (at least KEY_CONFIG_OFFSET + KEY_CONFIG_SIZE bytes).
        // nullopt when the dump is too short or (row, col) is outside the matrix.
        [[nodiscard]] static std::optional<KeyTravelConfig> decodeKeyTravel(std::span<const std::uint8_t> profile,
                                                                           std::size_t row, std::size_t col) noexcept;
        static bool encodeKeyTravel(std::span<std::uint8_t> profile, std::size_t row, std::size_t col,
                                    const KeyTravelConfig& config) noexcept;

        // SOCD entry `index` (0 .. SOCD_COUNT - 1) of a whole profile dump.
        [[nodiscard]] static std::optional<SocdPair> decodeSocd(std::span<const std::uint8_t> profile, std::size_t index) noexcept;
        static bool encodeSocd(std::span<std::uint8_t> profile, std::size_t index, const SocdPair& pair) noexcept;

        // Dynamic keystroke `index` (0 .. OKMC_COUNT - 1) of a whole profile dump.
        [[nodiscard]] static std::optional<OkmcConfig> decodeOkmc(std::span<const std::uint8_t> profile, std::size_t index) noexcept;
        static bool encodeOkmc(std::span<std::uint8_t> profile, std::size_t index, const OkmcConfig& config) noexcept;
        [[nodiscard]] static std::array<std::uint8_t, OKMC_ENTRY_SIZE> encodeOkmcEntry(const OkmcConfig& config) noexcept;
        [[nodiscard]] static OkmcConfig decodeOkmcEntry(std::span<const std::uint8_t, OKMC_ENTRY_SIZE> bytes) noexcept;

        // The values the key scanner really uses for a key (analog_matrix.c: update_key_config): every per-key
        // field that is 0 is taken from the global settings; a release sensitivity of 0 means "same as press".
        [[nodiscard]] static KeyTravelConfig resolveKeyTravel(const GlobalTravelConfig& global,
                                                              const KeyTravelConfig& key) noexcept;
    };

    static_assert(ProfileMemory::OKMC_OFFSET == 388);
    static_assert(ProfileMemory::SOCD_OFFSET == 768);
    static_assert(ProfileMemory::NAME_OFFSET == 828);
    static_assert(ProfileMemory::PROFILE_SIZE == 860, "must match sizeof(analog_matrix_profile_t) on the K2 HE");

} // namespace kbd
