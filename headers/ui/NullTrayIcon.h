#pragma once

#include "ui/TrayIconBase.h"

namespace ui {

    // Tray backend for sessions without a system tray (headless servers, unsupported platforms).
    // start() reports failure so callers can fall back to other ways of reaching the UI.
    class NullTrayIcon final : public TrayIconBase {
    public:
        NullTrayIcon() = default;
        ~NullTrayIcon() override = default;

        NullTrayIcon(const NullTrayIcon&) = delete;
        NullTrayIcon& operator=(const NullTrayIcon&) = delete;
        NullTrayIcon(NullTrayIcon&&) = delete;
        NullTrayIcon& operator=(NullTrayIcon&&) = delete;

        bool start() override;
        void stop() override;
        [[nodiscard]] bool isRunning() const noexcept override;
        [[nodiscard]] std::string_view backendName() const noexcept override;

    private:
        void onStateChanged(unsigned changes) override;
    };

} // namespace ui
