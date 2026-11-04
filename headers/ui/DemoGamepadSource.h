#pragma once

#include "ui/GamepadModel.h"

#include <cstdint>

namespace ui {

    // A scripted controller for --demo and screenshots: the left stick plays fighting-game motions the way a
    // keyboard produces them (digital, reaching the corners), the right stick circles, the triggers ramp like
    // analog keys and the face buttons fire on the motions, so every part of the gamepad page has something
    // to show without hardware.
    class DemoGamepadSource final : public gamepad::IGamepadSource {
    public:
        DemoGamepadSource();

        [[nodiscard]] std::vector<gamepad::PadInfo> pads() const override;
        [[nodiscard]] std::optional<std::uint32_t> selectedPad() const override;
        void selectPad(std::uint32_t id) override;
        [[nodiscard]] gamepad::PadState current() const override;
        [[nodiscard]] std::vector<gamepad::Sample> takeSamples() override;
        [[nodiscard]] std::uint64_t nowNs() const override;
        [[nodiscard]] std::string backendName() const override;
        void setActive(bool active) override;

        // The scripted state `elapsedNs` after the script started (exposed for tests).
        [[nodiscard]] static gamepad::PadState stateAt(std::uint64_t elapsedNs) noexcept;

    private:
        std::uint64_t m_startNs;
        std::uint64_t m_lastSampleNs;
    };

} // namespace ui
