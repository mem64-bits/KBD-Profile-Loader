#include "ui/NullTrayIcon.h"

namespace ui {

bool NullTrayIcon::start() {
    return false;
}

void NullTrayIcon::stop() {}

bool NullTrayIcon::isRunning() const noexcept {
    return false;
}

std::string_view NullTrayIcon::backendName() const noexcept {
    return "none";
}

void NullTrayIcon::onStateChanged(unsigned) {}

} // namespace ui
