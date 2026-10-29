#pragma once

#include <vector>
#include <array>
#include <span>
#include <cstdint>
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace kbd {

class KeychronProtocol {
public:
    static constexpr size_t RAW_PACKET_SIZE       = 32;
    static constexpr size_t PROFILE_MEMORY_SIZE   = 860;
    static constexpr size_t MAX_CHUNK_PAYLOAD     = 26;
    static constexpr size_t TOTAL_CHUNKS          = 34; // 33 * 26B + 1 * 2B

    // Command Opcodes
    static constexpr uint8_t CMD_ANALOG_MATRIX          = 0xA9; // AMC Subsystem
    static constexpr uint8_t CMD_SNAPTAP_CONFIG         = 0xA8; // SnapTap / SOCD

    // Official Keychron AMC (Analog Matrix Controller) Subcommands
    static constexpr uint8_t AMC_GET_VERSION            = 0x01;
    static constexpr uint8_t AMC_GET_PROFILES_INFO      = 0x10; // Query active slot & count
    static constexpr uint8_t AMC_SELECT_PROFILE         = 0x11; // Fast slot select
    static constexpr uint8_t AMC_GET_PROFILE_RAW        = 0x12; // 860B EEPROM chunk streaming
    static constexpr uint8_t AMC_SET_PROFILE_NAME       = 0x13; // Set ASCII name string on MCU
    static constexpr uint8_t AMC_SET_TRAVEL             = 0x14; // Single-packet fast actuation/RT tuning
    static constexpr uint8_t AMC_SET_ADVANCE_MODE       = 0x15; // Advanced trigger mode
    static constexpr uint8_t AMC_CLEAR_PROFILE          = 0x1D; // Clear profile RAM buffer
    static constexpr uint8_t AMC_SAVE_PROFILE           = 0x1F; // Commit RAM staging buffer to SPI Flash
    static constexpr uint8_t AMC_GET_CURVE              = 0x20;
    static constexpr uint8_t AMC_SET_CURVE              = 0x21;
    static constexpr uint8_t AMC_GET_REALTIME_TRAVEL    = 0x30; // Live mm travel reading
    static constexpr uint8_t AMC_CALIBRATE              = 0x40;
    static constexpr uint8_t AMC_GET_CALIBRATE_STATE    = 0x41;

    // A8 SnapTap Subcommands
    static constexpr uint8_t SUBCMD_SNAPTAP_RGB         = 0x06;
    static constexpr uint8_t SUBCMD_SNAPTAP_TABLE       = 0x09;

    // 1. Slot Selection & Status Query
    [[nodiscard]] static std::vector<uint8_t> createSlotSelectPacket(uint8_t slotIndex);
    [[nodiscard]] static std::vector<uint8_t> createStatusQueryPacket();

    // 2. Commit Staging RAM to Board Flash/EEPROM (0x1F)
    [[nodiscard]] static std::vector<uint8_t> createSaveProfilePacket(uint8_t slotIndex);

    // 3. Fast Single-Packet Actuation & Rapid Trigger Tuning (0x14)
    // actPointTenths: 3 = 0.3mm, 14 = 1.4mm
    // pressSensTenths: 1 = 0.10mm, 2 = 0.20mm
    // releaseSensTenths: 1 = 0.10mm, 2 = 0.20mm
    [[nodiscard]] static std::vector<uint8_t> createFastTravelPacket(
        uint8_t slotIndex,
        bool rapidTrigger,
        uint8_t actPointTenths,
        uint8_t pressSensTenths,
        uint8_t releaseSensTenths,
        bool entireKeyboard = true
    );

    // 4. Set Hardware Profile Name on MCU (0x13)
    [[nodiscard]] static std::vector<uint8_t> createSetProfileNamePacket(
        uint8_t slotIndex,
        const std::string& name
    );

    // 5. 860-Byte Chunking Serialization (0x12)
    [[nodiscard]] static std::vector<uint8_t> createMemoryChunkPacket(
        uint8_t slotIndex,
        uint16_t offset,
        std::span<const uint8_t> chunkData
    );
    [[nodiscard]] static std::vector<std::vector<uint8_t>> generateMemoryStreamPackets(
        uint8_t slotIndex,
        std::span<const uint8_t, PROFILE_MEMORY_SIZE> memoryBuffer
    );
    [[nodiscard]] static std::array<uint8_t, PROFILE_MEMORY_SIZE> extractMemoryBuffer(
        const nlohmann::json& jsonConfig
    );

    // 6. SOCD & Live Polling
    [[nodiscard]] static std::vector<uint8_t> createSocdPairPacket(uint8_t mode, uint8_t keyIndex1, uint8_t keyIndex2);
    [[nodiscard]] static std::vector<uint8_t> createRealtimeTravelPollPacket();
};

} // namespace kbd