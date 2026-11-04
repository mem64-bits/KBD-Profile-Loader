#include "ui/imgui/SdlGamepadSource.h"
#include "files/Logger.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <map>
#include <string>

namespace ui {

namespace {

    using namespace gamepad;

    constexpr std::size_t MAX_PENDING_SAMPLES = 2048; // nobody reads them while the gamepad page is closed

    PadFamily familyOf(const SDL_GamepadType type) noexcept {
        switch (type) {
            case SDL_GAMEPAD_TYPE_XBOX360:
            case SDL_GAMEPAD_TYPE_XBOXONE:
                return PadFamily::Xbox;
            case SDL_GAMEPAD_TYPE_PS3:
            case SDL_GAMEPAD_TYPE_PS4:
            case SDL_GAMEPAD_TYPE_PS5:
                return PadFamily::PlayStation;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
                return PadFamily::Nintendo;
            default:
                return PadFamily::Generic;
        }
    }

    float stickValue(const Sint16 raw) noexcept {
        return std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f);
    }

    float triggerValue(const Sint16 raw) noexcept {
        return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
    }

    // SDL reports stick Y with "down" positive; the model uses "up" positive.
    float modelAxis(const std::size_t axis, const Sint16 raw) noexcept {
        switch (static_cast<Axis>(axis)) {
            case Axis::LeftY:
            case Axis::RightY:
                return -stickValue(raw);
            case Axis::LeftTrigger:
            case Axis::RightTrigger:
                return triggerValue(raw);
            default:
                return stickValue(raw);
        }
    }

} // namespace

struct SdlGamepadSource::Impl {
    bool attached{false};  // SDL itself is initialised (UIManager::run is in its loop)
    bool wanted{false};    // setActive(true) was the last call
    bool running{false};   // SDL_INIT_GAMEPAD is up

    std::map<SDL_JoystickID, SDL_Gamepad*> open;
    std::optional<SDL_JoystickID>          selected;
    bool                                   userChoice{false}; // the user picked a pad: never switch on our own
    PadState                               state;
    std::vector<Sample>                    samples;

    void start() {
        if (running || !attached || !wanted) return;
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            LOG_WARN("Gamepad", std::string("Could not start SDL's gamepad support: ") + SDL_GetError());
            return;
        }
        running = true;
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
            for (int i = 0; i < count; ++i) openPad(ids[i]);
            SDL_free(ids);
        }
        LOG_INFO("Gamepad", "Listening for game controllers (" + std::to_string(open.size()) + " connected).");
    }

    void stop() {
        if (!running) return;
        for (auto& [id, pad] : open) SDL_CloseGamepad(pad);
        open.clear();
        selected.reset();
        state = {};
        samples.clear();
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        running = false;
    }

    void openPad(const SDL_JoystickID id) {
        if (open.contains(id)) return;
        SDL_Gamepad* pad = SDL_OpenGamepad(id);
        if (pad == nullptr) {
            LOG_WARN("Gamepad", std::string("Could not open a controller: ") + SDL_GetError());
            return;
        }
        open.emplace(id, pad);
        const char* name = SDL_GetGamepadName(pad);
        LOG_INFO("Gamepad", std::string("Controller connected: ") + (name != nullptr ? name : "unnamed"));
        if (!selected || (!userChoice && SDL_GetGamepadVendor(pad) == KEYCHRON_VENDOR_ID && SDL_GetGamepadVendor(open.at(*selected)) != KEYCHRON_VENDOR_ID)) {
            choose(id);
        }
    }

    void closePad(const SDL_JoystickID id) {
        const auto it = open.find(id);
        if (it == open.end()) return;
        SDL_CloseGamepad(it->second);
        open.erase(it);
        if (selected && *selected == id) {
            selected.reset();
            userChoice = false;
            record(SDL_GetTicksNS(), PadState{}); // the history sees everything let go
            state = {};
            if (!open.empty()) choose(bestPad());
        }
    }

    [[nodiscard]] SDL_JoystickID bestPad() const {
        for (const auto& [id, pad] : open) {
            if (SDL_GetGamepadVendor(pad) == KEYCHRON_VENDOR_ID) return id;
        }
        return open.begin()->first;
    }

    void choose(const SDL_JoystickID id) {
        selected = id;
        SDL_Gamepad* pad = open.at(id);
        PadState fresh;
        for (std::size_t b = 0; b < BUTTON_COUNT; ++b) fresh.buttons[b] = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b));
        for (std::size_t a = 0; a < AXIS_COUNT; ++a) fresh.axes[a] = modelAxis(a, SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(a)));
        state = fresh;
        samples.clear();
        record(SDL_GetTicksNS(), state);
    }

    void record(const std::uint64_t timestampNs, const PadState& value) {
        samples.push_back({timestampNs, value});
        if (samples.size() > MAX_PENDING_SAMPLES) samples.erase(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - MAX_PENDING_SAMPLES));
    }
};

SdlGamepadSource::SdlGamepadSource() : m_impl(std::make_unique<Impl>()) {}

SdlGamepadSource::~SdlGamepadSource() = default;

void SdlGamepadSource::attach() {
    m_impl->attached = true;
    m_impl->start();
}

void SdlGamepadSource::detach() {
    m_impl->stop();
    m_impl->attached = false;
}

void SdlGamepadSource::setActive(const bool active) {
    m_impl->wanted = active;
    if (active) {
        m_impl->start();
    } else {
        m_impl->stop();
    }
}

void SdlGamepadSource::handleEvent(const SDL_Event& event) {
    Impl& s = *m_impl;
    if (!s.running) return;

    switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            s.openPad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            s.closePad(event.gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            if (!s.selected || event.gaxis.which != *s.selected) break;
            const auto axis = static_cast<std::size_t>(event.gaxis.axis);
            if (axis >= AXIS_COUNT) break;
            const float value = modelAxis(axis, event.gaxis.value);
            if (s.state.axes[axis] == value) break;
            s.state.axes[axis] = value;
            s.record(event.gaxis.timestamp, s.state);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            if (!s.selected || event.gbutton.which != *s.selected) break;
            const auto button = static_cast<std::size_t>(event.gbutton.button);
            if (button >= BUTTON_COUNT) break;
            if (s.state.buttons[button] == event.gbutton.down) break;
            s.state.buttons[button] = event.gbutton.down;
            s.record(event.gbutton.timestamp, s.state);
            break;
        }
        default:
            break;
    }
}

std::vector<PadInfo> SdlGamepadSource::pads() const {
    std::vector<PadInfo> list;
    for (const auto& [id, pad] : m_impl->open) {
        PadInfo info;
        info.id      = static_cast<std::uint32_t>(id);
        const char* name = SDL_GetGamepadName(pad);
        info.name    = name != nullptr ? name : "Controller";
        info.vendor  = SDL_GetGamepadVendor(pad);
        info.product = SDL_GetGamepadProduct(pad);
        info.family  = familyOf(SDL_GetGamepadType(pad));
        list.push_back(std::move(info));
    }
    return list;
}

std::optional<std::uint32_t> SdlGamepadSource::selectedPad() const {
    if (!m_impl->selected) return std::nullopt;
    return static_cast<std::uint32_t>(*m_impl->selected);
}

void SdlGamepadSource::selectPad(const std::uint32_t id) {
    const auto joystick = static_cast<SDL_JoystickID>(id);
    if (!m_impl->open.contains(joystick)) return;
    m_impl->userChoice = true;
    if (m_impl->selected && *m_impl->selected == joystick) return;
    m_impl->choose(joystick);
}

PadState SdlGamepadSource::current() const {
    return m_impl->state;
}

std::vector<Sample> SdlGamepadSource::takeSamples() {
    std::vector<Sample> taken;
    taken.swap(m_impl->samples);
    return taken;
}

std::uint64_t SdlGamepadSource::nowNs() const {
    return SDL_GetTicksNS();
}

std::string SdlGamepadSource::backendName() const {
    const int version = SDL_GetVersion();
    return "SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(version)) + "." +
           std::to_string(SDL_VERSIONNUM_MICRO(version));
}

} // namespace ui
