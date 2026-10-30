#include "ui/TrayMenuBuilder.h"

#include <algorithm>
#include <array>

namespace ui::tray {

namespace {

    constexpr int SEPARATOR_ID_BASE = 100;
    constexpr int HEADER_ID_BASE    = 200;

    constexpr const char* DEVICE_NAME = "Keychron K2 HE";

    std::string slotEntry(const AppSnapshot& state, const std::size_t slot) {
        const SlotInfo& info = state.slots[slot];
        return slotLabel(slot) + " - " + (info.hasProfile ? info.name : std::string("no profile"));
    }

} // namespace

int idFor(const TrayAction action) noexcept {
    return static_cast<int>(action);
}

TrayMenu buildMenu(const AppSnapshot& state, const bool autostartEnabled) {
    TrayMenu menu;
    int nextSeparatorId = SEPARATOR_ID_BASE;
    int nextHeaderId    = HEADER_ID_BASE;

    const auto header = [&](std::string label) {
        menu.items.push_back({nextHeaderId++, TrayMenuItem::Kind::Header, std::move(label), false, false, TrayAction::None});
    };
    const auto separator = [&] {
        menu.items.push_back({nextSeparatorId++, TrayMenuItem::Kind::Separator, {}, false, false, TrayAction::None});
    };
    const auto action = [&](const TrayAction what, std::string label, const bool enabled = true) {
        menu.items.push_back({idFor(what), TrayMenuItem::Kind::Action, std::move(label), enabled, false, what});
    };
    const auto choice = [&](const TrayAction what, std::string label, const bool enabled, const bool selected) {
        menu.items.push_back({idFor(what), TrayMenuItem::Kind::Radio, std::move(label), enabled, selected, what});
    };

    const bool connected = state.device.connected;
    const bool canSwitch = connected && !state.busy;

    header(connected ? std::string(DEVICE_NAME) + " - " + transportName(state.device.transport)
                     : std::string(DEVICE_NAME) + " - disconnected");
    if (connected && state.activeSlot >= 0 && state.activeSlot < static_cast<int>(SLOT_COUNT)) {
        header("Active: " + slotEntry(state, static_cast<std::size_t>(state.activeSlot)));
    }
    separator();

    constexpr std::array<TrayAction, SLOT_COUNT> SLOT_ACTIONS{TrayAction::ActivateSlot1, TrayAction::ActivateSlot2,
                                                              TrayAction::ActivateSlot3};
    for (std::size_t slot = 0; slot < SLOT_COUNT; ++slot) {
        choice(SLOT_ACTIONS[slot], slotEntry(state, slot), canSwitch, state.activeSlot == static_cast<int>(slot));
    }

    const bool anyProfile = std::any_of(state.slots.begin(), state.slots.end(),
                                        [](const SlotInfo& slot) { return slot.hasProfile; });
    action(TrayAction::NextProfile, "Next profile", canSwitch && anyProfile);
    action(TrayAction::PreviousProfile, "Previous profile", canSwitch && anyProfile);
    separator();

    action(TrayAction::OpenDashboard, "Open dashboard...");
    action(TrayAction::OpenProfilesFolder, "Open profiles folder");
    action(TrayAction::OpenConfigFile, "Edit config.ini");
    action(TrayAction::OpenLogFile, "View daemon.log");
    separator();

    menu.items.push_back({idFor(TrayAction::ToggleAutostart), TrayMenuItem::Kind::Check, "Run at startup", true,
                          autostartEnabled, TrayAction::ToggleAutostart});
    separator();

    action(TrayAction::Quit, "Exit");
    return menu;
}

std::string tooltipTitle() {
    return DEVICE_NAME;
}

std::string tooltipDescription(const AppSnapshot& state) {
    if (!state.device.connected) return "Not connected";

    const std::string transport = std::string(" (") + transportName(state.device.transport) + ")";
    if (state.activeSlot < 0 || state.activeSlot >= static_cast<int>(SLOT_COUNT)) return "Connected" + transport;

    const SlotInfo& slot = state.slots[static_cast<std::size_t>(state.activeSlot)];
    const std::string label = slotLabel(static_cast<std::size_t>(state.activeSlot));
    return (slot.hasProfile ? label + ": " + slot.name : label) + transport;
}

} // namespace ui::tray
