#pragma once

#include <string>
#include <vector>

namespace ui {

    // What choosing a menu entry means. The TrayPresenter maps these to application behaviour; the
    // tray backends never interpret them, they only report which item id was chosen.
    enum class TrayAction {
        None,
        OpenDashboard,
        ActivateSlot1,
        ActivateSlot2,
        ActivateSlot3,
        NextProfile,
        PreviousProfile,
        OpenProfilesFolder,
        OpenConfigFile,
        OpenLogFile,
        ToggleAutostart,
        Quit
    };

    // One row of the tray's context menu: plain data, identical on every operating system.
    struct TrayMenuItem {
        enum class Kind {
            Action,    // clickable entry
            Separator,
            Header,    // non-clickable caption
            Check,     // entry with a check mark (checked = on)
            Radio      // entry with a radio mark (checked = selected)
        };

        int         id{0};     // unique within the menu and > 0 (the D-Bus menu protocol reserves 0 for the root)
        Kind        kind{Kind::Action};
        std::string label;
        bool        enabled{true};
        bool        checked{false};
        TrayAction  action{TrayAction::None};

        bool operator==(const TrayMenuItem&) const = default;
    };

    struct TrayMenu {
        std::vector<TrayMenuItem> items;

        [[nodiscard]] const TrayMenuItem* find(int id) const;

        bool operator==(const TrayMenu&) const = default;
    };

} // namespace ui
