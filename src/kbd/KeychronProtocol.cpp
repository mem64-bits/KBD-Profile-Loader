#include "kbd/KeychronProtocol.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <cstring>

#include "kbd/ProfileMemory.h"

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
    // Firmware travel mode (AKM_*): 1 = regular, 2 = Rapid Trigger. 0 ("follow global") is rejected for
    // the whole keyboard (analog_matrix/profile.c: profile_set_traval).
    packet[3] = rapidTrigger ? TRAVEL_MODE_RAPID : TRAVEL_MODE_REGULAR;
    packet[4] = actPointTenths;               // actuation: 3 = 0.3 mm, 14 = 1.4 mm
    packet[5] = pressSensTenths;              // press sensitivity: 1 = 0.1 mm
    packet[6] = releaseSensTenths;            // release sensitivity: 1 = 0.1 mm
    packet[7] = entireKeyboard ? 0x01 : 0x00; // 1 = global settings of the profile
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createKeyTravelPacket(
    uint8_t slotIndex,
    uint8_t mode,
    uint8_t actPointTenths,
    uint8_t pressSensTenths,
    uint8_t releaseSensTenths,
    const std::array<uint32_t, MATRIX_ROWS>& rowMasks
) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_TRAVEL; // 0x14
    packet[2] = slotIndex % 3;
    packet[3] = mode;           // 0 = follow global, 1 = regular, 2 = Rapid Trigger
    packet[4] = actPointTenths; // 0 = follow global
    packet[5] = pressSensTenths;
    packet[6] = releaseSensTenths;
    packet[7] = 0x00;           // per key: the masks below select the keys

    // analog_matrix.c copies 3 bytes per row into a little-endian uint32_t: bit c = column c.
    for (std::size_t row = 0; row < MATRIX_ROWS; ++row) {
        const std::size_t at = KEY_MASK_OFFSET + row * KEY_MASK_BYTES_PER_ROW;
        packet[at + 0] = static_cast<uint8_t>(rowMasks[row] & 0xFF);
        packet[at + 1] = static_cast<uint8_t>((rowMasks[row] >> 8) & 0xFF);
        packet[at + 2] = static_cast<uint8_t>((rowMasks[row] >> 16) & 0xFF);
    }
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createAdvanceModePacket(uint8_t slotIndex, uint8_t advMode, uint8_t row, uint8_t col,
                                                               uint8_t index) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_ADVANCE_MODE; // 0x15: profile_set_adv_mode(&data[2])
    packet[2] = slotIndex % 3;
    packet[3] = advMode;
    packet[4] = row;
    packet[5] = col;
    packet[6] = index;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createSocdPacket(uint8_t slotIndex, uint8_t index, uint8_t row1, uint8_t col1,
                                                        uint8_t row2, uint8_t col2, uint8_t type) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_SOCD; // 0x16: profile_set_socd(&data[2])
    packet[2] = slotIndex % 3;
    packet[3] = row1;
    packet[4] = col1;
    packet[5] = row2;
    packet[6] = col2;
    packet[7] = index;
    packet[8] = type;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createProfileReadPacket(uint8_t slotIndex, uint16_t offset, uint8_t size) {
    if (size > MAX_CHUNK_PAYLOAD) {
        throw std::invalid_argument("A profile read returns at most 26 bytes per report");
    }
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_PROFILE_RAW; // 0x12: the reply carries the bytes at [6..]
    packet[2] = slotIndex % 3;
    packet[3] = static_cast<uint8_t>(offset & 0xFF);
    packet[4] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    packet[5] = size;
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

    // profile_set_name(data[2], data[3], &data[4]): [3] = length (at most 28), [4..] = the text.
    const size_t copyLen = std::min(name.length(), MAX_PROFILE_NAME_LENGTH);
    packet[3] = static_cast<uint8_t>(copyLen);
    std::memcpy(packet.data() + 4, name.data(), copyLen);
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
    packet[1] = AMC_GET_PROFILE_RAW; // 0x12 (the firmware treats it as a read; see createProfileReadPacket)
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

namespace {

    // Integer field of a Launcher-style JSON object; missing, null or non-numeric values give `fallback`.
    int intField(const nlohmann::json& object, const char* key, const int fallback) {
        const auto it = object.find(key);
        if (it == object.end() || !it->is_number()) return fallback;
        return it->get<int>();
    }

    // One Launcher travel object ({mode, distance, advance_press, advance_release, advanceMode, dksIndex,
    // dirty, edit}). In the Launcher's exports the top-level distances of a key that follows the slot are
    // filled in with the slot's values for display, while `edit` keeps the raw values read from the keyboard
    // (0 = "use the global value"). So the raw copy wins unless the entry has unapplied edits (`dirty`).
    GlobalTravelConfig travelFromLauncherJson(const nlohmann::json& object, const int defaultMode) {
        const auto editIt = object.find("edit");
        const bool dirty  = object.contains("dirty") && object["dirty"].is_boolean() && object["dirty"].get<bool>();
        const nlohmann::json& values = (editIt != object.end() && editIt->is_object() && !dirty) ? *editIt : object;
        const auto field = [&](const char* key, const int fallback) { return intField(values, key, intField(object, key, fallback)); };

        GlobalTravelConfig config;
        config.mode              = static_cast<uint8_t>(field("mode", defaultMode) & 0x03);
        config.actuationTenths   = static_cast<uint8_t>(field("distance", 0) & 0x3F);
        config.pressSensTenths   = static_cast<uint8_t>(field("advance_press", 0) & 0x3F);
        config.releaseSensTenths = static_cast<uint8_t>(field("advance_release", 0) & 0x3F);
        config.advancedMode      = static_cast<uint8_t>(intField(object, "advanceMode", 0) & 0x0F);
        config.advancedData      = static_cast<uint8_t>(intField(object, "dksIndex", 0) & 0xFF);
        return config;
    }

    // Launcher "snapTap": [{keys: [{row, col, ...}, {row, col, ...}], type, index}, ...] -> socd[index].
    void socdFromLauncherJson(std::array<uint8_t, KeychronProtocol::PROFILE_MEMORY_SIZE>& buffer, const nlohmann::json& pairs) {
        if (!pairs.is_array()) return;
        for (const auto& entry : pairs) {
            if (!entry.is_object() || !entry.contains("keys") || !entry["keys"].is_array() || entry["keys"].size() != 2) continue;
            const int index = intField(entry, "index", -1);
            const int type  = intField(entry, "type", 0);
            const auto& k1 = entry["keys"][0];
            const auto& k2 = entry["keys"][1];
            const int r1 = intField(k1, "row", -1), c1 = intField(k1, "col", -1);
            const int r2 = intField(k2, "row", -1), c2 = intField(k2, "col", -1);
            const auto inMatrix = [](const int r, const int c) {
                return r >= 0 && c >= 0 && static_cast<std::size_t>(r) < ProfileMemory::MATRIX_ROWS &&
                       static_cast<std::size_t>(c) < ProfileMemory::MATRIX_COLS;
            };
            if (index < 0 || static_cast<std::size_t>(index) >= ProfileMemory::SOCD_COUNT || type <= SocdPair::TYPE_NONE ||
                type > SocdPair::TYPE_MAX || !inMatrix(r1, c1) || !inMatrix(r2, c2)) {
                continue;
            }
            ProfileMemory::encodeSocd(buffer, static_cast<std::size_t>(index),
                                      SocdPair{static_cast<uint8_t>(r1), static_cast<uint8_t>(c1), static_cast<uint8_t>(r2),
                                               static_cast<uint8_t>(c2), static_cast<uint8_t>(type)});
        }
    }

    void writeTravel(std::array<uint8_t, KeychronProtocol::PROFILE_MEMORY_SIZE>& buffer, const std::size_t offset,
                     const GlobalTravelConfig& config) {
        const auto bytes = ProfileMemory::encodeGlobalTravel(config);
        std::copy(bytes.begin(), bytes.end(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
    }

} // namespace

std::array<uint8_t, KeychronProtocol::PROFILE_MEMORY_SIZE> KeychronProtocol::extractMemoryBuffer(
    const nlohmann::json& jsonConfig
) {
    static_assert(PROFILE_MEMORY_SIZE == ProfileMemory::PROFILE_SIZE);
    std::array<uint8_t, PROFILE_MEMORY_SIZE> buffer{};

    // -------------------------------------------------------------------------------------------------
    // Format A: Keychron Launcher style ("global" + "keyMatrix"), laid out like analog_matrix_profile_t:
    // global @0, key_config[row][col] @4 + (row * 16 + col) * 4, SOCD pairs ("snapTap") @768, name @828.
    // Dynamic-keystroke data ("dks") and the CRC are not converted and stay zero.
    // -------------------------------------------------------------------------------------------------
    if (jsonConfig.contains("global") && jsonConfig["global"].is_object()) {
        writeTravel(buffer, ProfileMemory::GLOBAL_TRAVEL_OFFSET,
                    travelFromLauncherJson(jsonConfig["global"], GlobalTravelConfig::MODE_REGULAR));

        if (jsonConfig.contains("keyMatrix") && jsonConfig["keyMatrix"].is_array()) {
            const auto& matrix = jsonConfig["keyMatrix"];
            for (std::size_t r = 0; r < matrix.size(); ++r) {
                if (!matrix[r].is_array()) continue;
                for (std::size_t c = 0; c < matrix[r].size(); ++c) {
                    const auto& key = matrix[r][c];
                    if (!key.is_object()) continue;

                    // Entries carry their own position in the Launcher; fall back to where they sit in the array.
                    const int row = intField(key, "row", static_cast<int>(r));
                    const int col = intField(key, "col", static_cast<int>(c));
                    if (row < 0 || col < 0 || static_cast<std::size_t>(row) >= ProfileMemory::MATRIX_ROWS ||
                        static_cast<std::size_t>(col) >= ProfileMemory::MATRIX_COLS) {
                        continue;
                    }
                    // Per-key 0 = "use the global value", which is also the Launcher's default for a key.
                    writeTravel(buffer, ProfileMemory::keyConfigOffset(static_cast<std::size_t>(row), static_cast<std::size_t>(col)),
                                travelFromLauncherJson(key, GlobalTravelConfig::MODE_GLOBAL));
                }
            }
        }

        if (jsonConfig.contains("snapTap")) socdFromLauncherJson(buffer, jsonConfig["snapTap"]);

        const std::string name = jsonConfig.value("name", std::string{});
        const std::size_t nameLength = std::min(name.size(), MAX_PROFILE_NAME_LENGTH);
        std::memcpy(buffer.data() + ProfileMemory::NAME_OFFSET, name.data(), nameLength);
        return buffer;
    }

    // -------------------------------------------------------------------------------------------------
    // Format B: raw dump as an array of numbers ("memory": [...])
    // -------------------------------------------------------------------------------------------------
    if (jsonConfig.contains("memory") && jsonConfig["memory"].is_array()) {
        const auto& mem = jsonConfig["memory"];
        const size_t bytesToRead = std::min(mem.size(), PROFILE_MEMORY_SIZE);
        for (size_t i = 0; i < bytesToRead; ++i) {
            buffer[i] = mem[i].get<uint8_t>();
        }
        return buffer;
    }

    // -------------------------------------------------------------------------------------------------
    // Format C: raw dump as a hex string ("raw_hex": "...")
    // -------------------------------------------------------------------------------------------------
    if (jsonConfig.contains("raw_hex") && jsonConfig["raw_hex"].is_string()) {
        const std::string hexStr = jsonConfig["raw_hex"].get<std::string>();
        for (size_t i = 0; i < PROFILE_MEMORY_SIZE && (i * 2 + 1) < hexStr.length(); ++i) {
            const std::string byteStr = hexStr.substr(i * 2, 2);
            buffer[i] = static_cast<uint8_t>(std::stoul(byteStr, nullptr, 16));
        }
        return buffer;
    }

    throw std::runtime_error("Profile JSON has none of 'global' (Launcher format), 'memory' or 'raw_hex'");
}

std::vector<uint8_t> KeychronProtocol::createOkmcPacket(const uint8_t slotIndex, const uint8_t row, const uint8_t col,
                                                        const uint8_t index, const OkmcConfig& config) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_ADVANCE_MODE; // profile_set_adv_mode(): data[5..8] travel, [9..16] keycodes, [17..24] actions
    packet[2] = slotIndex % 3;
    packet[3] = ADV_MODE_OKMC;
    packet[4] = row;
    packet[5] = col;
    packet[6] = index;
    packet[7]  = config.shallowActTenths & 0x3F;
    packet[8]  = config.shallowDeactTenths & 0x3F;
    packet[9]  = config.deepActTenths & 0x3F;
    packet[10] = config.deepDeactTenths & 0x3F;
    const auto entry = ProfileMemory::encodeOkmcEntry(config); // keycodes and actions in their stored layout
    std::copy(entry.begin() + 3, entry.end(), packet.begin() + 11);
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createRealtimeTravelPacket(const uint8_t row, const uint8_t col) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_REALTIME_TRAVEL; // get_realtime_travel(&data[2])
    packet[2] = row;
    packet[3] = col;
    return packet;
}

std::optional<KeychronProtocol::RealtimeTravel> KeychronProtocol::parseRealtimeTravel(const std::span<const uint8_t> reply,
                                                                                      const uint8_t row, const uint8_t col) {
    if (reply.size() < 14 || reply[0] != CMD_ANALOG_MATRIX || reply[1] != AMC_GET_REALTIME_TRAVEL) return std::nullopt;
    if (reply[2] != 0 || reply[3] != row || reply[4] != col) return std::nullopt;
    RealtimeTravel travel;
    travel.row          = reply[3];
    travel.col          = reply[4];
    travel.travelTenths = reply[5];
    travel.travelRaw    = reply[6];
    travel.sensorValue  = static_cast<uint16_t>(reply[7] | (reply[8] << 8));
    travel.zeroValue    = static_cast<uint16_t>(reply[9] | (reply[10] << 8));
    travel.fullValue    = static_cast<uint16_t>(reply[11] | (reply[12] << 8));
    travel.pressed      = reply[13] == 0x11;
    return travel;
}

std::vector<uint8_t> KeychronProtocol::createCurveReadPacket() {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_CURVE;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createCurvePacket(const ResponseCurve& curve) {
    if (!isValidCurve(curve)) throw std::invalid_argument("response curve points out of range or out of order");
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_CURVE; // game_controller_set_curve((point_t*)&data[2])
    for (std::size_t i = 0; i < curve.size(); ++i) {
        packet[2 + i * 2] = curve[i].x;
        packet[3 + i * 2] = curve[i].y;
    }
    return packet;
}

std::optional<ResponseCurve> KeychronProtocol::parseCurve(const std::span<const uint8_t> reply) {
    if (reply.size() < 10 || reply[0] != CMD_ANALOG_MATRIX || reply[1] != AMC_GET_CURVE) return std::nullopt;
    ResponseCurve curve{};
    for (std::size_t i = 0; i < curve.size(); ++i) curve[i] = {reply[2 + i * 2], reply[3 + i * 2]};
    if (!isValidCurve(curve)) return std::nullopt;
    return curve;
}

std::vector<uint8_t> KeychronProtocol::createControllerModeReadPacket() {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_GET_GAME_CONTROLLER_MODE;
    return packet;
}

std::vector<uint8_t> KeychronProtocol::createControllerModePacket(const uint8_t mode) {
    std::vector<uint8_t> packet(RAW_PACKET_SIZE, 0x00);
    packet[0] = CMD_ANALOG_MATRIX;
    packet[1] = AMC_SET_GAME_CONTROLLER_MODE; // game_controller_mode_set(data[2])
    packet[2] = mode & ControllerMode::KNOWN_BITS;
    return packet;
}

std::optional<uint8_t> KeychronProtocol::parseControllerMode(const std::span<const uint8_t> reply) {
    if (reply.size() < 4 || reply[0] != CMD_ANALOG_MATRIX || reply[1] != AMC_GET_GAME_CONTROLLER_MODE || reply[2] != 0) return std::nullopt;
    return reply[3];
}

} // namespace kbd