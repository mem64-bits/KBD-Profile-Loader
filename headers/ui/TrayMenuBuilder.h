#pragma once

#include "ui/AppState.h"
#include "ui/TrayMenu.h"

#include <string>

namespace ui::tray {

    // Item id of an action entry (the numeric value of the enum), stable across rebuilds so tray hosts
    // that cache menus can diff them.
    [[nodiscard]] int idFor(TrayAction action) noexcept;

    // The context menu for the current application state. Pure function: the same snapshot always
    // yields the same menu, which is what lets the presenter skip redundant updates.
    [[nodiscard]] TrayMenu buildMenu(const AppSnapshot& state, bool autostartEnabled);

    [[nodiscard]] std::string tooltipTitle();
    [[nodiscard]] std::string tooltipDescription(const AppSnapshot& state);

} // namespace ui::tray
