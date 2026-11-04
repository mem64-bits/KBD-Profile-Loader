#include "kbd/KeyboardDevice.h"
#include <hidapi.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace kbd {

constexpr uint16_t KEYCHRON_VID   = 0x3434;
constexpr uint16_t QMK_USAGE_PAGE = 0xFF60; // raw HID (VIA / Keychron configuration) interface
constexpr uint16_t QMK_USAGE      = 0x61;

// The wired K2 HE, one product id per layout (keyboards/keychron/k2_he/{ansi,iso,jis}/keyboard.json). The
// configuration interface only exists over the cable, so these are the only devices the app opens.
struct WiredModel {
    uint16_t        productId;
    KeyboardVariant variant;
};
constexpr WiredModel WIRED_MODELS[] = {
    {0x0E20, KeyboardVariant::Ansi},
    {0x0E21, KeyboardVariant::Iso},
    {0x0E22, KeyboardVariant::Jis},
};

// 1. Constructor
KeyboardDevice::KeyboardDevice() {
    hid_init();
}

// 2. Destructor
KeyboardDevice::~KeyboardDevice() {
    disconnect();
    hid_exit();
}

// 3. Move Operations (Thread-safe)
KeyboardDevice::KeyboardDevice(KeyboardDevice&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.m_ioMutex);
    m_handle       = other.m_handle;
    m_devicePath   = std::move(other.m_devicePath);
    m_productId    = other.m_productId;
    m_isConnected  = other.m_isConnected;
    m_dryRun       = other.m_dryRun;
    m_dumpFilePath = std::move(other.m_dumpFilePath);

    other.m_handle = nullptr;
    other.m_isConnected = false;
}

KeyboardDevice& KeyboardDevice::operator=(KeyboardDevice&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(m_ioMutex, other.m_ioMutex);
        if (m_handle != nullptr) {
            hid_close(m_handle);
            m_handle = nullptr;
        }
        m_handle       = other.m_handle;
        m_devicePath   = std::move(other.m_devicePath);
        m_productId    = other.m_productId;
        m_isConnected  = other.m_isConnected;
        m_dryRun       = other.m_dryRun;
        m_dumpFilePath = std::move(other.m_dumpFilePath);

        other.m_handle = nullptr;
        other.m_isConnected = false;
    }
    return *this;
}

// 4. Dry-Run Control & Accessors
void KeyboardDevice::setDryRun(bool enabled, const std::string& dumpFilePath) {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_dryRun = enabled;
    m_dumpFilePath = dumpFilePath;
    if (m_dryRun) {
        std::cout << "[KeyboardDevice] *** DRY RUN MODE ENABLED *** (Packets logged to " << m_dumpFilePath << ")\n";
    }
}

bool KeyboardDevice::isDryRun() const noexcept {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    return m_dryRun;
}

bool KeyboardDevice::isConnected() const noexcept {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    return m_isConnected;
}

std::string KeyboardDevice::getDevicePath() const {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    return m_devicePath;
}

uint16_t KeyboardDevice::productId() const noexcept {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    return m_productId;
}

KeyboardVariant KeyboardDevice::variant() const noexcept {
    return variantForProductId(productId());
}

KeyboardVariant KeyboardDevice::variantForProductId(const uint16_t productId) noexcept {
    for (const WiredModel& model : WIRED_MODELS) {
        if (model.productId == productId) return model.variant;
    }
    return KeyboardVariant::Unknown;
}

// 5. Endpoint Discovery & Active Probing
std::vector<KeyboardDevice::CandidateEndpoint> KeyboardDevice::findAllCandidates() {
    std::vector<CandidateEndpoint> candidates;
    for (const WiredModel& model : WIRED_MODELS) {
        hid_device_info* devices = hid_enumerate(KEYCHRON_VID, model.productId);
        for (const hid_device_info* current = devices; current != nullptr; current = current->next) {
            if (current->usage_page != QMK_USAGE_PAGE || current->usage != QMK_USAGE || current->path == nullptr) continue;
            candidates.push_back({std::string(current->path), current->product_id, current->interface_number});
        }
        hid_free_enumeration(devices);
    }
    return candidates;
}

bool KeyboardDevice::probeEndpoint(hid_device* handle) {
    if (!handle) return false;

    // AMC_GET_PROFILES_INFO (0xA9 0x10): every K2 HE firmware answers it.
    uint8_t probePacket[33] = {0x00};
    probePacket[1] = 0xA9;
    probePacket[2] = 0x10;
    if (hid_write(handle, probePacket, sizeof(probePacket)) < 0) return false;

    // Up to ~100 ms for the answer; replies meant for other programs on the same interface are skipped.
    uint8_t reply[32] = {0x00};
    for (int attempt = 0; attempt < 5; ++attempt) {
        const int bytesRead = hid_read_timeout(handle, reply, sizeof(reply), 20);
        if (bytesRead < 0) return false;
        if (bytesRead >= 2 && reply[0] == 0xA9 && reply[1] == 0x10) return true;
    }
    return false;
}

bool KeyboardDevice::isPresent() const {
    std::string path;
    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        if (m_dryRun) return true;
        if (!m_isConnected) return false;
        path = m_devicePath;
    }
    for (const CandidateEndpoint& candidate : findAllCandidates()) {
        if (candidate.path == path) return true;
    }
    return false;
}

bool KeyboardDevice::hasJoystickCollection(const std::span<const std::pair<uint16_t, uint16_t>> usages) noexcept {
    for (const auto& [page, usage] : usages) {
        if (page == 0x01 && (usage == 0x04 || usage == 0x05)) return true; // Generic Desktop: Joystick / Game Pad
    }
    return false;
}

std::optional<bool> KeyboardDevice::enumeratedAsXInput() const {
    uint16_t productId = 0;
    {
        std::lock_guard<std::mutex> lock(m_ioMutex);
        if (m_dryRun || !m_isConnected) return std::nullopt;
        productId = m_productId;
    }
    std::vector<std::pair<uint16_t, uint16_t>> usages;
    hid_device_info* devices = hid_enumerate(KEYCHRON_VID, productId);
    for (const hid_device_info* current = devices; current != nullptr; current = current->next) {
        usages.emplace_back(current->usage_page, current->usage);
    }
    hid_free_enumeration(devices);
    if (usages.empty()) return std::nullopt;
    return !hasJoystickCollection(usages);
}

// 6. Connect & Disconnect Lifecycle
bool KeyboardDevice::connect() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (m_handle != nullptr) {
        hid_close(m_handle);
        m_handle = nullptr;
    }
    m_isConnected = false;
    m_devicePath.clear();
    m_productId = 0;

    const auto candidates = findAllCandidates();
    if (candidates.empty()) return false; // not plugged in (the watcher keeps looking quietly)

    // Prefer an endpoint that answers; a keyboard that is still booting may not, so fall back to the first one.
    const CandidateEndpoint* chosen = nullptr;
    for (const auto& candidate : candidates) {
        hid_device* handle = hid_open_path(candidate.path.c_str());
        if (handle == nullptr) continue;
        if (probeEndpoint(handle)) {
            m_handle = handle;
            chosen = &candidate;
            break;
        }
        hid_close(handle);
    }
    if (m_handle == nullptr) {
        m_handle = hid_open_path(candidates.front().path.c_str());
        if (m_handle == nullptr) return false;
        chosen = &candidates.front();
    }

    m_devicePath  = chosen->path;
    m_productId   = chosen->productId;
    m_isConnected = true;
    std::cout << "[KeyboardDevice] Connected to the K2 HE (USB PID 0x" << std::hex << std::setw(4) << std::setfill('0')
              << m_productId << std::dec << "): " << m_devicePath << '\n';
    return true;
}

void KeyboardDevice::disconnect() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (m_handle != nullptr) {
        hid_close(m_handle);
        m_handle = nullptr;
    }
    m_devicePath.clear();
    m_productId = 0;
    m_isConnected = false;
}

// 7. HID Read & Write Pipes
bool KeyboardDevice::sendReport(const std::vector<uint8_t>& packet) {
    std::lock_guard<std::mutex> lock(m_ioMutex);

    if (m_dryRun) {
        std::ofstream logFile(m_dumpFilePath, std::ios::app);
        if (logFile.is_open()) {
            logFile << "REPORT [size=" << packet.size() << "]: ";
            for (auto b : packet) {
                logFile << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
            }
            logFile << "\n";
        }
        return true;
    }

    if (!m_isConnected || m_handle == nullptr) {
        std::cerr << "[KeyboardDevice] Cannot send: device not connected.\n";
        return false;
    }

    uint8_t buffer[33] = {0x00};
    size_t bytesToCopy = std::min(packet.size(), size_t(32));
    std::memcpy(buffer + 1, packet.data(), bytesToCopy);

    int result = hid_write(m_handle, buffer, sizeof(buffer));
    if (result < 0) {
        std::cerr << "[KeyboardDevice] USB write failed.\n";
        m_isConnected = false;
        return false;
    }
    return true;
}

int KeyboardDevice::readReport(std::vector<uint8_t>& outBuffer, int timeoutMs) {
    std::lock_guard<std::mutex> lock(m_ioMutex);

    if (m_dryRun) {
        outBuffer.assign(32, 0x00);
        outBuffer[0] = 0xA9;
        outBuffer[1] = 0x10;
        outBuffer[2] = 0x00; // Mock Slot 0
        return 32;
    }

    if (!m_isConnected || m_handle == nullptr) {
        return -1;
    }

    outBuffer.assign(32, 0x00);
    return hid_read_timeout(m_handle, outBuffer.data(), outBuffer.size(), timeoutMs);
}

    void KeyboardDevice::flushInputBuffer() const
    {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (!m_isConnected || m_handle == nullptr) return;

    uint8_t dump[32];
    // Drain any stale packets sitting in the Windows OS HID ring buffer
    while (hid_read_timeout(m_handle, dump, sizeof(dump), 0) > 0) {
        // Intentionally discarded
    }
}

} // namespace kbd