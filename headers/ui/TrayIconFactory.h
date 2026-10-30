#pragma once

#include "ui/ITrayIcon.h"

#include <memory>

namespace ui {

    // Picks the tray backend for the platform this binary was built for: Shell_NotifyIcon on Windows,
    // StatusNotifierItem over D-Bus on Linux (when built with GIO), otherwise a NullTrayIcon whose
    // start() reports that no tray is available.
    [[nodiscard]] std::unique_ptr<ITrayIcon> createTrayIcon();

} // namespace ui
