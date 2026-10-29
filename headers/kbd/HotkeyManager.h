#pragma once

#if defined(_WIN32)

#include <functional>
#include <thread>
#include <atomic>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace kbd {

    class HotkeyManager {
    public:
        using ActionCallback = std::function<void()>;

        // 6-parameter constructor matching HotkeyManager.cpp
        HotkeyManager(ActionCallback onNext, ActionCallback onPrev,
                      UINT nextModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, UINT nextVk = VK_OEM_6,
                      UINT prevModifiers = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, UINT prevVk = VK_OEM_4);
        ~HotkeyManager();

        HotkeyManager(const HotkeyManager&) = delete;
        HotkeyManager& operator=(const HotkeyManager&) = delete;

        void start();
        void stop();

        [[nodiscard]] bool isRunning() const noexcept;

    private:
        void threadWorker(const std::stop_token& stopToken);

        ActionCallback     m_onNext;
        ActionCallback     m_onPrev;
        UINT               m_nextMod;
        UINT               m_nextVk;
        UINT               m_prevMod;
        UINT               m_prevVk;

        std::jthread       m_thread;
        std::atomic<bool>  m_running{false};
        DWORD              m_threadId{0};
    };

} // namespace kbd

#endif // _WIN32