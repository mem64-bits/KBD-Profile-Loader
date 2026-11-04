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

// Wired only: every interval, check that the connected keyboard is still enumerated (no packets are sent, so
// nothing interleaves with the controller's HID transactions), or try to open it when it is not connected.
void DeviceWatcher::run(std::stop_token stopToken) {
    m_wasConnected = m_device.isConnected(); // main() may have connected before the watcher started

    while (!stopToken.stop_requested()) {
        std::this_thread::sleep_for(m_interval);
        if (stopToken.stop_requested()) break;

        if (m_device.isConnected()) {
            if (m_device.isPresent()) continue;
            m_device.disconnect();
        } else if (m_device.connect()) {
            const std::string path = m_device.getDevicePath();
            m_wasConnected   = true;
            m_lastActivePath = path;
            std::lock_guard<std::mutex> lock(m_observerMutex);
            m_observers.forEach([&path](IDeviceObserver* observer) {
                if (observer) observer->onDeviceConnected(TransportMode::Wired, path);
            });
            continue;
        }

        if (m_wasConnected) {
            m_wasConnected = false;
            m_lastActivePath.clear();
            std::lock_guard<std::mutex> lock(m_observerMutex);
            m_observers.forEach([](IDeviceObserver* observer) {
                if (observer) observer->onDeviceDisconnected();
            });
        }
    }
}

} // namespace kbd
