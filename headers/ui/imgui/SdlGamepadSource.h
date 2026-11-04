#pragma once

#include "ui/GamepadModel.h"

#include <memory>

union SDL_Event;

namespace ui {

    // Game controllers through SDL3's gamepad API (XInput, Windows.Gaming.Input, HIDAPI, evdev, ...).
    // Owned by UIManager, which feeds it every SDL event on the UI thread. The gamepad subsystem only runs
    // while the dashboard is visible (setActive), so a hidden tray daemon does not wake up for every stick
    // movement while you play.
    //
    // With the K2 HE in XInput mode the keyboard itself shows up here as an Xbox-style controller (vendor
    // 0x3434) and is picked automatically; any other controller can be selected instead.
    class SdlGamepadSource final : public gamepad::IGamepadSource {
    public:
        SdlGamepadSource();
        ~SdlGamepadSource() override;

        SdlGamepadSource(const SdlGamepadSource&) = delete;
        SdlGamepadSource& operator=(const SdlGamepadSource&) = delete;
        SdlGamepadSource(SdlGamepadSource&&) = delete;
        SdlGamepadSource& operator=(SdlGamepadSource&&) = delete;

        // UI thread only, from UIManager::run(): SDL's video subsystem is up / about to go down.
        void attach();
        void detach();
        void handleEvent(const SDL_Event& event);

        // --- IGamepadSource ---
        [[nodiscard]] std::vector<gamepad::PadInfo> pads() const override;
        [[nodiscard]] std::optional<std::uint32_t> selectedPad() const override;
        void selectPad(std::uint32_t id) override;
        [[nodiscard]] gamepad::PadState current() const override;
        [[nodiscard]] std::vector<gamepad::Sample> takeSamples() override;
        [[nodiscard]] std::uint64_t nowNs() const override;
        [[nodiscard]] std::string backendName() const override;
        void setActive(bool active) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace ui
