#include "kbd/KeychronProtocol.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <cstring>

namespace kbd {

std::vector<uint8_t> KeychronProtocol::createSlotSelectPacket(uint8_t slotIndex) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SELECT_PROFILE; // 0x11
    packet[2] = slotIndex % 3;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createStatusQueryPacket() {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_PROFILES_INFO; // 0x10
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createSaveProfilePacket(uint8_t slotIndex) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SAVE_PROFILE; // 0x1F (Commits RAM to Flash/EEPROM)
    packet[2] = slotIndex % 3;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createFastTravelPacket(
    uint8_t slotIndex,
    bool rapidTrigger,
    uint8_t actPointTenths,
    uint8_t pressSensTenths,
    uint8_t releaseSensTenths,
    bool entireKeyboard
) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_TRAVEL; // 0x14
    packet[2] = slotIndex % 3;
    packet[3] = rapidTrigger ? 0x01 : 0x00;  // mode: 1 = RT enabled, 0 = standard
    packet[4] = actPointTenths;             // actuation: 3 = 0.3mm, 14 = 1.4mm
    packet[5] = pressSensTenths;            // press sensitivity: 1 = 0.10mm
    packet[6] = releaseSensTenths;          // release sensitivity: 1 = 0.10mm
    packet[7] = entireKeyboard ? 0x01 : 0x00; // 1 = global entire keyboard
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createSetProfileNamePacket(
    uint8_t slotIndex,
    const std::string& name
) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_PROFILE_NAME; // 0x13
    packet[2] = slotIndex % 3;

    // Bytes 3..31: Up to 28 characters of ASCII string
    size_t copyLen = std::min(name.length(), size_t(28));
    std::memcpy(packet.data() + 3, name.data(), copyLen);
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createMemoryChunkPacket(
    uint8_t slotIndex,
    uint16_t offset,
    std::span<const uint8_t> chunkData
) {
    if (chunkData.size() > MAX_CHUNK_PAYLOAD) {
        throw std::invalid_argument("Chunk payload exceeds 26-byte buffer boundary");
    }

    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_PROFILE_RAW; // 0x12
    packet[2] = slotIndex % 3;
    packet[3] = static_cast<uint8_t>(offset & 0xFF);        // Offset Lo
    packet[4] = static_cast<uint8_t>((offset >> 8) & 0xFF); // Offset Hi
    packet[5] = static_cast<uint8_t>(chunkData.size());

    std::copy(chunkData.begin(), chunkData.end(), packet.begin() + 6);
    return packet;
}

std::vector<std::vector<uint8_t>> KeychronProtocol::generateMemoryStreamPackets(
    uint8_t slotIndex,
    std::span<const uint8_t, PROFILE_MEMORY_SIZE> memoryBuffer
) {
    std::vector<std::vector<uint8_t>> packets;
    packets.reserve(TOTAL_CHUNKS);

    size_t offset = 0;
    while (offset < PROFILE_MEMORY_SIZE) {
        size_t chunkSize = std::min(MAX_CHUNK_PAYLOAD, PROFILE_MEMORY_SIZE - offset);
        std::span<const uint8_t> chunkView(memoryBuffer.data() + offset, chunkSize);

        packets.push_back(createMemoryChunkPacket(
            slotIndex,
            static_cast<uint16_t>(offset),
            chunkView
        ));

        offset += chunkSize;
    }

    return packets;
}

std::array<uint8_t, KeychronProtocol::PROFILE_MEMORY_SIZE> KeychronProtocol::extractMemoryBuffer(
    const nlohmann::json& jsonConfig
) {
    std::array<uint8_t, PROFILE_MEMORY_SIZE> buffer{};
    buffer.fill(0x00);

    if (jsonConfig.contains("memory") && jsonConfig["memory"].is_array()) {
        const auto& mem = jsonConfig["memory"];
        size_t bytesToRead = std::min(mem.size(), PROFILE_MEMORY_SIZE);
        for (size_t i = 0; i < bytesToRead; ++i) {
            buffer[i] = mem[i].get<uint8_t>();
        }
    } else if (jsonConfig.contains("raw_hex") && jsonConfig["raw_hex"].is_string()) {
        std::string hexStr = jsonConfig["raw_hex"].get<std::string>();
        for (size_t i = 0; i < PROFILE_MEMORY_SIZE && (i * 2 + 1) < hexStr.length(); ++i) {
            std::string byteStr = hexStr.substr(i * 2, 2);
            buffer[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        }
    } else {
        throw std::runtime_error("Invalid profile JSON format: missing 'memory' or 'raw_hex'");
    }

    return buffer;
}

std::vector<uint8_t> KeychronProtocol::createSocdPairPacket(uint8_t mode, uint8_t keyIndex1, uint8_t keyIndex2) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_SNAPTAP_CONFIG;
    packet[1] = SUBCMD_SNAPTAP_TABLE; // 0x09
    packet[2] = mode;
    packet[3] = keyIndex1;
    packet[4] = keyIndex2;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createRealtimeTravelPollPacket() {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_REALTIME_TRAVEL; // 0x30 (True live mm travel polling)
    return packet;
}

} // namespace kbd