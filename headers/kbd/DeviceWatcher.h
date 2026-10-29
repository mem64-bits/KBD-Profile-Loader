#pragma once

#include "kbd/IDeviceObserver.h"
#include "dsa/DLL.h"

#include <thread>
#include <mutex>
#include <string>
#include <chrono>

namespace kbd {

    class KeyboardDevice;

    class DeviceWatcher {
    public:
        explicit DeviceWatcher(KeyboardDevice& device, std::chrono::milliseconds interval = std::chrono::milliseconds(1000));
        ~DeviceWatcher();

        DeviceWatcher(const DeviceWatcher&) = delete;
        DeviceWatcher& operator=(const DeviceWatcher&) = delete;

        void addObserver(IDeviceObserver* observer);
        void removeObserver(IDeviceObserver* observer);

        void start();
        void stop();

    private:
        void run(std::stop_token stopToken);

        KeyboardDevice&              m_device;
        std::chrono::milliseconds   m_interval;
        std::jthread                 m_thread;
        std::mutex                   m_observerMutex;
        dsa::DLL<IDeviceObserver*>   m_observers;
        std::string                  m_lastActivePath{};
        bool                         m_wasConnected{false};
    };

} // namespace kbd