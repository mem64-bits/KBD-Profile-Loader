#include "ui/DemoProfileGateway.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <thread>

namespace ui {

namespace {

    void setKeys(KeyMatrix& matrix, std::initializer_list<KeyPosition> keys, const KeyActuation& value) {
        for (const KeyPosition& key : keys) matrix.at(key) = value;
    }

    // Matrix positions on the K2 HE (same for ANSI and ISO in the alpha block).
    constexpr KeyPosition KEY_W{2, 2}, KEY_A{3, 1}, KEY_S{3, 2}, KEY_D{3, 3}, KEY_SPACE{5, 6};
    constexpr KeyPosition KEY_U{2, 7}, KEY_I{2, 8}, KEY_J{3, 7}, KEY_K{3, 8};
    constexpr KeyPosition KEY_Q{2, 1}, KEY_E{2, 3}, KEY_R{2, 4}, KEY_LSHIFT{4, 0}, KEY_LCTRL{5, 0};
    constexpr KeyPosition KEY_H{3, 6}, KEY_L{3, 9}, KEY_F{3, 4};

    void setGamepad(SlotDetails& details, const KeyPosition key, const std::uint8_t target) {
        details.set(key, {kbd::AdvancedMode::GAMEPAD, target});
    }

    void addSocd(SlotDetails& details, const std::size_t index, const KeyPosition a, const KeyPosition b, const std::uint8_t type) {
        details.socd[index] = kbd::SocdPair{a.row, a.col, b.row, b.col, type};
    }

} // namespace

DemoProfileGateway::DemoProfileGateway(const std::chrono::milliseconds latency, const int activeSlot)
    : m_latency(latency), m_active(((activeSlot % static_cast<int>(SLOT_COUNT)) + static_cast<int>(SLOT_COUNT)) % static_cast<int>(SLOT_COUNT)) {
    DemoProfile standard{"Default", "default.json", 0, ActuationSettings{false, 20, 4, 4}, {}};

    // Fighting game: movement keys on a hair trigger, the four attack buttons a little deeper.
    DemoProfile tekken{"Tekken 8 Mishima", "tekken8.json", 1, ActuationSettings{true, 3, 1, 1}, {}};
    setKeys(tekken.keys, {KEY_W, KEY_A, KEY_S, KEY_D}, KeyActuation{KeyActuation::MODE_RAPID, 2, 1, 1});
    setKeys(tekken.keys, {KEY_SPACE}, KeyActuation{KeyActuation::MODE_RAPID, 4, 1, 1});
    setKeys(tekken.keys, {KEY_U, KEY_I, KEY_J, KEY_K}, KeyActuation{KeyActuation::MODE_REGULAR, 7, 0, 0});
    tekken.extras.socdPairs = 3; // A/D, W/S, Space/S
    addSocd(tekken.details, 0, KEY_A, KEY_D, 6);     // neutral, as in a real Tekken export
    addSocd(tekken.details, 1, KEY_SPACE, KEY_S, 6);
    addSocd(tekken.details, 2, KEY_W, KEY_S, 6);

    // Shooter: Rapid Trigger on movement, abilities with a deeper, safer actuation.
    DemoProfile valorant{"Valorant", "valorant.json", std::nullopt, ActuationSettings{true, 10, 3, 3}, {}};
    setKeys(valorant.keys, {KEY_W, KEY_A, KEY_S, KEY_D}, KeyActuation{KeyActuation::MODE_RAPID, 5, 1, 2});
    setKeys(valorant.keys, {KEY_Q, KEY_E, KEY_R}, KeyActuation{KeyActuation::MODE_REGULAR, 25, 0, 0});
    setKeys(valorant.keys, {KEY_LSHIFT, KEY_LCTRL}, KeyActuation{KeyActuation::MODE_REGULAR, 15, 0, 0});

    // Open-world game: WASD is an analog left stick (walk slowly, run at the bottom), F / Space / Shift are
    // buttons, keys also type so menus still work; W also has "sprint on deep press" as a dynamic keystroke
    // alternative on the arrow-up key.
    DemoProfile analog{"Elden Ring (analog)", "eldenring.json", 2, ActuationSettings{false, 15, 3, 3}, {}};
    setGamepad(analog.details, KEY_W, kbd::GamepadTarget::LEFT_STICK_UP);
    setGamepad(analog.details, KEY_A, kbd::GamepadTarget::LEFT_STICK_LEFT);
    setGamepad(analog.details, KEY_S, kbd::GamepadTarget::LEFT_STICK_DOWN);
    setGamepad(analog.details, KEY_D, kbd::GamepadTarget::LEFT_STICK_RIGHT);
    setGamepad(analog.details, KEY_SPACE, kbd::GamepadTarget::button(0)); // A
    setGamepad(analog.details, KEY_F, kbd::GamepadTarget::button(2));     // X
    setGamepad(analog.details, KEY_LSHIFT, kbd::GamepadTarget::button(1)); // B
    setGamepad(analog.details, KEY_H, kbd::GamepadTarget::RIGHT_STICK_LEFT);
    setGamepad(analog.details, KEY_L, kbd::GamepadTarget::RIGHT_STICK_RIGHT);
    analog.details.okmc[0] = kbd::deepPressCombo(kbd::Keycode::UP, kbd::Keycode::LEFT_SHIFT, 10, 30);
    analog.details.set(KeyPosition{4, 13}, {kbd::AdvancedMode::DKS, 0}); // arrow up
    analog.extras.gamepadKeys = 9;
    analog.extras.dynamicKeys = 1;

    DemoProfile typing{"Typing (deep)", "typing.json", std::nullopt, ActuationSettings{false, 25, 4, 4}, {}};
    DemoProfile sf6{"Street Fighter 6", "sf6.json", std::nullopt, ActuationSettings{true, 5, 2, 2}, {}};
    setKeys(sf6.keys, {KEY_W, KEY_A, KEY_S, KEY_D}, KeyActuation{KeyActuation::MODE_RAPID, 3, 1, 1});

    m_library = {standard, tekken, analog, valorant, typing, sf6};
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        if (const DemoProfile* profile = profileForSlot(i)) {
            m_hardware[i]        = profile->global;
            m_hardwareKeys[i]    = profile->keys;
            m_hardwareDetails[i] = profile->details;
        }
    }

    // Start the way the automatic gamepad mode would leave the keyboard: XInput on only for a gamepad slot.
    const SlotDetails& start = m_hardwareDetails[static_cast<std::size_t>(m_active)];
    const bool padKeys = std::any_of(start.keys.begin(), start.keys.end(),
                                     [](const KeyAdvanced& key) { return key.mode == kbd::AdvancedMode::GAMEPAD; });
    m_controllerMode = padKeys ? std::uint8_t{kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING}
                               : std::uint8_t{kbd::ControllerMode::TYPING};
    m_enumeratedXInput = padKeys;
}

DemoProfileGateway::~DemoProfileGateway() = default;

void DemoProfileGateway::simulateLatency() const {
    std::this_thread::sleep_for(m_latency);
}

const DemoProfileGateway::DemoProfile* DemoProfileGateway::profileForSlot(const std::size_t slot) const {
    const auto it = std::find_if(m_library.begin(), m_library.end(),
                                 [slot](const DemoProfile& p) { return p.slot && *p.slot == slot; });
    return it == m_library.end() ? nullptr : &*it;
}

DeviceInfo DemoProfileGateway::describeDevice() const {
    return {true, kbd::TransportMode::Wired, "(demo keyboard)", kbd::KeyboardVariant::Iso};
}

std::array<SlotInfo, SLOT_COUNT> DemoProfileGateway::describeSlots() const {
    std::array<SlotInfo, SLOT_COUNT> slots;
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
        slots[i].name = slotLabel(i);
        if (const DemoProfile* profile = profileForSlot(i)) {
            slots[i] = SlotInfo{true, profile->name, profile->fileName, profile->global, profile->keys};
        }
    }
    return slots;
}

int DemoProfileGateway::queryActiveSlot() {
    return m_active;
}

CommandResult DemoProfileGateway::activateSlot(const std::uint8_t slot) {
    simulateLatency();
    m_active = slot % static_cast<int>(SLOT_COUNT);
    const DemoProfile* profile = profileForSlot(static_cast<std::size_t>(m_active));
    return {true, slotLabel(static_cast<std::size_t>(m_active)) + " is active" + (profile ? " (" + profile->name + ")." : ".")};
}

CommandResult DemoProfileGateway::cycleProfile(const bool forward) {
    simulateLatency();
    const int count = static_cast<int>(SLOT_COUNT);
    m_active = (m_active + (forward ? 1 : count - 1)) % count;
    const DemoProfile* profile = profileForSlot(static_cast<std::size_t>(m_active));
    return {true, "Switched to " + (profile ? profile->name : slotLabel(static_cast<std::size_t>(m_active))) + " (" +
                  slotLabel(static_cast<std::size_t>(m_active)) + ")."};
}

CommandResult DemoProfileGateway::applyActuation(const std::uint8_t slot, const ActuationSettings& settings) {
    simulateLatency();
    m_hardware[slot % SLOT_COUNT] = actuation::sanitized(settings);
    return {true, "Actuation applied."};
}

std::optional<ActuationSettings> DemoProfileGateway::readActuation(const std::uint8_t slot, const std::optional<ActuationSettings>&) {
    simulateLatency();
    return m_hardware[slot % SLOT_COUNT];
}

CommandResult DemoProfileGateway::applyKeyActuation(const std::uint8_t slot, const std::vector<KeyPosition>& keys,
                                                    const KeyActuation& value) {
    simulateLatency();
    if (keys.empty()) return {false, "No keys selected."};
    for (const KeyPosition& key : keys) m_hardwareKeys[slot % SLOT_COUNT].at(key) = actuation::sanitized(value);
    return {true, std::to_string(keys.size()) + (keys.size() == 1 ? " key" : " keys") + " of " + slotLabel(slot % SLOT_COUNT) + " updated."};
}

std::optional<KeyMatrix> DemoProfileGateway::readKeyMatrix(const std::uint8_t slot) {
    simulateLatency();
    return m_hardwareKeys[slot % SLOT_COUNT];
}

CommandResult DemoProfileGateway::saveSlot(const std::uint8_t slot) {
    simulateLatency();
    return {true, slotLabel(slot % SLOT_COUNT) + " saved to the keyboard's flash."};
}

CommandResult DemoProfileGateway::flashSlotProfile(const std::uint8_t slot) {
    simulateLatency();
    const std::size_t index = slot % SLOT_COUNT;
    const DemoProfile* profile = profileForSlot(index);
    if (profile == nullptr) return {false, "No profile file is assigned to " + slotLabel(index) + "."};

    m_hardware[index]        = profile->global;
    m_hardwareKeys[index]    = profile->keys;
    m_hardwareDetails[index] = profile->details;
    return {true, "'" + profile->name + "' written to " + slotLabel(index) + "."};
}

void DemoProfileGateway::handleFileCreated(const std::filesystem::path&) {}
void DemoProfileGateway::handleFileDeleted(const std::filesystem::path&) {}
void DemoProfileGateway::handleDeviceConnected(kbd::TransportMode, const std::string&) {}
void DemoProfileGateway::handleDeviceDisconnected() {}

std::vector<LibraryProfileInfo> DemoProfileGateway::describeLibrary() const {
    std::vector<LibraryProfileInfo> library;
    library.reserve(m_library.size());
    for (const DemoProfile& profile : m_library) {
        library.push_back({profile.name, profile.fileName, profile.slot, profile.global, profile.keys.overrideCount(), profile.extras});
    }
    return library;
}

CommandResult DemoProfileGateway::reorderLibrary(const std::size_t fromIndex, const std::size_t toIndex) {
    if (fromIndex >= m_library.size() || toIndex >= m_library.size() || fromIndex == toIndex) {
        return {false, "That profile cannot be moved there."};
    }
    DemoProfile moved = m_library[fromIndex];
    m_library.erase(m_library.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    m_library.insert(m_library.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(moved));
    return {true, "Profile order updated."};
}

CommandResult DemoProfileGateway::assignProfileToSlot(const std::string& fileName, const std::uint8_t slot, const bool writeNow) {
    const std::uint8_t target = static_cast<std::uint8_t>(slot % SLOT_COUNT);
    const auto it = std::find_if(m_library.begin(), m_library.end(), [&](const DemoProfile& p) { return p.fileName == fileName; });
    if (it == m_library.end()) return {false, "Profile file not found: " + fileName};

    for (DemoProfile& other : m_library) {
        if (other.slot && *other.slot == target) other.slot.reset();
    }
    it->slot = target;
    if (writeNow) return flashSlotProfile(target);
    return {true, "'" + it->name + "' is now the profile of " + slotLabel(target) + "."};
}

CommandResult DemoProfileGateway::clearSlot(const std::uint8_t slot) {
    const std::uint8_t target = static_cast<std::uint8_t>(slot % SLOT_COUNT);
    bool changed = false;
    for (DemoProfile& profile : m_library) {
        if (profile.slot && *profile.slot == target) {
            profile.slot.reset();
            changed = true;
        }
    }
    if (!changed) return {false, "No profile file is assigned to " + slotLabel(target) + "."};
    return {true, slotLabel(target) + " has no profile file now; the file stays in the library."};
}

std::optional<SlotDetails> DemoProfileGateway::readSlotDetails(const std::uint8_t slot) {
    simulateLatency();
    return m_hardwareDetails[slot % SLOT_COUNT];
}

CommandResult DemoProfileGateway::applyAdvancedKey(const std::uint8_t slot, const AdvancedKeyChange& change) {
    SlotDetails& details = m_hardwareDetails[slot % SLOT_COUNT];
    switch (change.kind) {
        case AdvancedKeyChange::Kind::Gamepad:
            if (!kbd::GamepadTarget::isValid(change.index)) return {false, "Unknown game-controller target."};
            details.set(change.key, {kbd::AdvancedMode::GAMEPAD, change.index});
            break;
        case AdvancedKeyChange::Kind::Toggle:
            details.set(change.key, {kbd::AdvancedMode::TOGGLE, 0});
            break;
        case AdvancedKeyChange::Kind::DynamicKeystroke:
            if (change.index >= details.okmc.size() || change.okmc.isEmpty()) return {false, "Invalid dynamic keystroke."};
            details.okmc[change.index] = change.okmc;
            details.set(change.key, {kbd::AdvancedMode::DKS, change.index});
            break;
        case AdvancedKeyChange::Kind::Clear:
            details.set(change.key, {});
            break;
    }
    return {true, "Key updated."};
}

CommandResult DemoProfileGateway::applySocd(const std::uint8_t slot, const std::size_t index, const kbd::SocdPair& pair) {
    SlotDetails& details = m_hardwareDetails[slot % SLOT_COUNT];
    if (index >= details.socd.size()) return {false, "SOCD entry out of range."};
    details.socd[index] = pair.isSet() ? pair : kbd::SocdPair{};
    return {true, "SOCD accepted."};
}

std::optional<ControllerSettings> DemoProfileGateway::readControllerSettings() {
    simulateLatency();
    return ControllerSettings{m_controllerMode, m_curve, std::nullopt, false};
}

CommandResult DemoProfileGateway::applyControllerMode(const std::uint8_t mode) {
    simulateLatency();
    m_controllerMode = mode & kbd::ControllerMode::KNOWN_BITS;
    return {true, "Controller mode accepted."};
}

CommandResult DemoProfileGateway::applyCurve(const kbd::ResponseCurve& curve) {
    simulateLatency();
    if (!kbd::isValidCurve(curve)) return {false, "The curve's points are out of order or out of range."};
    m_curve = curve;
    return {true, "Response curve accepted."};
}

std::optional<LiveKeyTravel> DemoProfileGateway::readLiveTravel(const KeyPosition& key) {
    // A slow press-and-release per key, out of phase with its neighbours.
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
    const double phase   = static_cast<double>(key.row * 3 + key.col) * 0.7;
    const double wave    = 0.5 - 0.5 * std::cos(seconds * std::numbers::pi * 0.9 + phase);
    const double tenths  = std::clamp(wave * 46.0 - 3.0, 0.0, 40.0); // rests at the top and the bottom for a moment
    LiveKeyTravel reading;
    reading.key          = key;
    reading.travelTenths = static_cast<std::uint8_t>(std::lround(tenths));
    reading.travelRaw    = static_cast<std::uint8_t>(std::lround(tenths * 6.0));
    reading.pressed      = tenths >= static_cast<double>(m_hardware[static_cast<std::size_t>(m_active)].actuationTenths);
    return reading;
}

std::optional<bool> DemoProfileGateway::enumeratedXInput() {
    return m_enumeratedXInput;
}

} // namespace ui
