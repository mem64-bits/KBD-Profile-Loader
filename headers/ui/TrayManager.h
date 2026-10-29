#pragma once

#if defined(_WIN32)

#include <string>
#include <thread>
#include <functional>
#include <atomic>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

// Forward declarations to decouple headers
namespace kbd { class ProfileService; }
namespace UI  { class UIManager; }

namespace kbd {

    class TrayManager {
    public:
        using ShutdownCallback = std::function<void()>;

        TrayManager(ProfileService& profileService, UI::UIManager& uiManager, ShutdownCallback onShutdown);
        ~TrayManager();

        TrayManager(const TrayManager&) = delete;
        TrayManager& operator=(const TrayManager&) = delete;

        void start();
        void stop();

        // Dynamically updates the hover tooltip when profiles change
        void updateStatus(const std::string& profileName, int activeSlot);

    private:
        void threadWorker(std::stop_token stopToken);
        static LRESULT CALLBACK windowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

        void showContextMenu();
        bool isStartupShortcutPresent() const;
        void toggleStartupShortcut();

        ProfileService&    m_profileService;
        UI::UIManager&     m_uiManager;
        ShutdownCallback   m_onShutdown;
        std::jthread       m_thread;
        std::atomic<bool>  m_running{false};

        HWND               m_hwnd{nullptr};
        NOTIFYICONDATAW    m_nid{};
        DWORD              m_threadId{0};
        int                m_currentSlot{0};
        std::string        m_currentProfileName{"Default"};
    };

} // namespace kbd

#endif // _WIN32