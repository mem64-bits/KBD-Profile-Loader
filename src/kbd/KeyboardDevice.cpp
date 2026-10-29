#include "kbd/KeyboardDevice.h"
#include <hidapi.h>
#include <iostream>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <algorithm>

namespace kbd {

constexpr uint16_t KEYCHRON_VID   = 0x3434;
constexpr uint16_t PID_K2_HE_WIRE = 0x0E21; // Direct USB-C Wired
constexpr uint16_t PID_K2_HE_DONG = 0xD030; // 2.4 GHz Wireless Dongle
constexpr uint16_t QMK_USAGE_PAGE = 0xFF60;
constexpr uint16_t QMK_USAGE      = 0x61;

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

// 5. Endpoint Discovery & Active Probing
std::vector<KeyboardDevice::CandidateEndpoint> KeyboardDevice::findAllCandidates() {
    std::vector<CandidateEndpoint> candidates;
    struct hid_device_info* devs = hid_enumerate(KEYCHRON_VID, 0x0);
    struct hid_device_info* curr = devs;

    while (curr != nullptr) {
        if (curr->usage_page == QMK_USAGE_PAGE && curr->usage == QMK_USAGE) {
            candidates.push_back({
                curr->path ? std::string(curr->path) : "",
                curr->product_id,
                curr->interface_number
            });
        }
        curr = curr->next;
    }

    hid_free_enumeration(devs);

    // Prioritize direct wired connection (PID 0x0E21) over dongle (PID 0xD030)
    std::sort(candidates.begin(), candidates.end(), [](const CandidateEndpoint& a, const CandidateEndpoint& b) {
        if (a.productId == PID_K2_HE_WIRE && b.productId != PID_K2_HE_WIRE) return true;
        if (a.productId != PID_K2_HE_WIRE && b.productId == PID_K2_HE_WIRE) return false;
        return a.interfaceNumber < b.interfaceNumber;
    });

    return candidates;
}

bool KeyboardDevice::probeEndpoint(hid_device* handle) {
    if (!handle) return false;

    // Send 33-byte Status Query packet (0xA9 0x10)
    uint8_t probePacket[33] = {0x00};
    probePacket[1] = 0xA9; // Vendor Opcode
    probePacket[2] = 0x10; // Status Query

    int written = hid_write(handle, probePacket, sizeof(probePacket));
    if (written < 0) return false;

    // Wait up to 40ms for the keyboard MCU to respond
    uint8_t readBuffer[32] = {0x00};
    int bytesRead = hid_read_timeout(handle, readBuffer, sizeof(readBuffer), 40);

    return (bytesRead > 0 && (readBuffer[0] == 0xA9 || readBuffer[1] == 0xA9));
}

// 6. Connect & Disconnect Lifecycle
bool KeyboardDevice::connect() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (m_isConnected) {
        if (m_handle != nullptr) {
            hid_close(m_handle);
            m_handle = nullptr;
        }
        m_isConnected = false;
        m_devicePath.clear();
    }

    auto candidates = findAllCandidates();
    if (candidates.empty()) {
        std::cerr << "[KeyboardDevice] No Keychron Raw HID (0xFF60:0x61) endpoints discovered.\n";
        return false;
    }

    // Probe each candidate with the 0xA9 0x10 heartbeat
    for (const auto& cand : candidates) {
        hid_device* testHandle = hid_open_path(cand.path.c_str());
        if (!testHandle) continue;

        if (probeEndpoint(testHandle)) {
            m_handle = testHandle;
            m_devicePath = cand.path;
            m_isConnected = true;
            std::cout << "[KeyboardDevice] Connected successfully to: " << m_devicePath << '\n';
            return true;
        }

        hid_close(testHandle);
    }

    // Fallback if keyboard is in low-power idle and didn't answer in 40ms
    m_handle = hid_open_path(candidates[0].path.c_str());
    if (m_handle != nullptr) {
        m_devicePath = candidates[0].path;
        m_isConnected = true;
        std::cout << "[KeyboardDevice] Fallback bound to: " << m_devicePath << '\n';
        return true;
    }

    return false;
}

void KeyboardDevice::disconnect() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    if (m_handle != nullptr) {
        hid_close(m_handle);
        m_handle = nullptr;
    }
    m_devicePath.clear();
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