#include "kbd/DeviceWatcher.h"
#include "kbd/KeyboardDevice.h"
#include "kbd/KeychronProtocol.h"
#include <iostream>

namespace kbd {

DeviceWatcher::DeviceWatcher(KeyboardDevice& device, std::chrono::milliseconds interval)
    : m_device(device), m_interval(interval)
{}

DeviceWatcher::~DeviceWatcher() {
    stop();
}

void DeviceWatcher::addObserver(IDeviceObserver* observer) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    if (observer && !m_observers.contains(observer)) {
        m_observers.addLast(observer);
    }
}

void DeviceWatcher::removeObserver(IDeviceObserver* observer) {
    std::lock_guard<std::mutex> lock(m_observerMutex);
    m_observers.remove(observer);
}

void DeviceWatcher::start() {
    if (m_thread.joinable()) return;
    m_thread = std::jthread([this](std::stop_token stopToken) { run(stopToken); });
    std::cout << "[DeviceWatcher] Hardware hot-plug watchdog started.\n";
}

void DeviceWatcher::stop() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
        std::cout << "[DeviceWatcher] Hardware watchdog stopped.\n";
    }
}

void DeviceWatcher::run(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(m_interval);
        if (stopToken.stop_requested()) break;

        // Check if current connection is still alive via a quick status check
        bool currentlyConnected = m_device.isConnected();

        if (currentlyConnected) {
            auto ping = KeychronProtocol::createStatusQueryPacket();
            if (!m_device.sendReport(ping)) {
                // Pipe broke!
                currentlyConnected = false;
                m_device.disconnect();
            }
        }

        // If disconnected, attempt to probe and hot-swap to active transport
        if (!currentlyConnected) {
            if (m_device.connect()) {
                std::string currentPath = m_device.getDevicePath();
                m_wasConnected = true;
                m_lastActivePath = currentPath;

                TransportMode mode = (currentPath.find("0E21") != std::string::npos || currentPath.find("0e21") != std::string::npos)
                                     ? TransportMode::Wired
                                     : TransportMode::WirelessDongle;

                std::lock_guard<std::mutex> lock(m_observerMutex);
                m_observers.forEach([mode, &currentPath](IDeviceObserver* obs) {
                    if (obs) obs->onDeviceConnected(mode, currentPath);
                });
            } else if (m_wasConnected) {
                m_wasConnected = false;
                m_lastActivePath.clear();

                std::lock_guard<std::mutex> lock(m_observerMutex);
                m_observers.forEach([](IDeviceObserver* obs) {
                    if (obs) obs->onDeviceDisconnected();
                });
            }
        }
    }
}

} // namespace kbd