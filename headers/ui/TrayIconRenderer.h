#pragma once

#include "ui/ITrayIcon.h"
#include "ui/Theme.h"

#include <array>
#include <vector>

namespace ui {

    // Sizes rendered for the tray; each backend uses the ones it needs (Windows picks the closest to the
    // system's small-icon size, the D-Bus tray sends all of them and lets the host choose).
    inline constexpr std::array<int, 5> TRAY_ICON_SIZES{16, 24, 32, 48, 64};

    // Draws the tray icon in the theme's colours: a black plate with a purple rim and one dot per profile
    // slot. The active slot's dot is lit; with the keyboard disconnected everything turns grey. Generated
    // in code, so no image files have to be shipped and the icon follows the theme.
    [[nodiscard]] TrayImage renderTrayIcon(int size, const Theme& theme, int activeSlot, bool connected);
    [[nodiscard]] std::vector<TrayImage> renderTrayIcons(const Theme& theme, int activeSlot, bool connected);

    // Area-averaging resize (exact for shrinking by whole factors; nearest-neighbour when enlarging).
    [[nodiscard]] TrayImage scaleTrayImage(const TrayImage& source, int size);

} // namespace ui
