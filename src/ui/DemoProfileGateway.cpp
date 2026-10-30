#include "ui/DemoProfileGateway.h"

#include <thread>

namespace ui {

DemoProfileGateway::DemoProfileGateway(const std::chrono::milliseconds latency) : m_latency(latency) {
    m_slots[0] = {true, "Default",          "default.json", ActuationSettings{false, 20, 4, 4}};
    m_slots[1] = {true, "Tekken 8 Mishima", "tekken8.json", ActuationSettings{true, 3, 1, 1}};
    m_slots[2] = {true, "Valorant",         "valorant.json", ActuationSettings{true, 10, 3, 3}};
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) m_hardware[i] = m_slots[i].storedSettings.value_or(ActuationSettings{});
}

DemoProfileGateway::~DemoProfileGateway() = default;

void DemoProfileGateway::simulateLatency() const {
    std::this_thread::sleep_for(m_latency);
}

DeviceInfo DemoProfileGateway::describeDevice() const {
    return {true, kbd::TransportMode::Wired, "(demo keyboard)"};
}

std::array<SlotInfo, SLOT_COUNT> DemoProfileGateway::describeSlots() const {
    return m_slots;
}

int DemoProfileGateway::queryActiveSlot() {
    return m_active;
}

CommandResult DemoProfileGateway::activateSlot(const std::uint8_t slot) {
    simulateLatency();
    m_active = slot % static_cast<int>(SLOT_COUNT);
    return {true, slotLabel(static_cast<std::size_t>(m_active)) + " is active (" + m_slots[static_cast<std::size_t>(m_active)].name + ")."};
}

CommandResult DemoProfileGateway::cycleProfile(const bool forward) {
    simulateLatency();
    const int count = static_cast<int>(SLOT_COUNT);
    m_active = (m_active + (forward ? 1 : count - 1)) % count;
    return {true, "Switched to " + m_slots[static_cast<std::size_t>(m_active)].name + " (" + slotLabel(static_cast<std::size_t>(m_active)) + ")."};
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

CommandResult DemoProfileGateway::saveSlot(const std::uint8_t slot) {
    simulateLatency();
    return {true, slotLabel(slot % SLOT_COUNT) + " saved to the keyboard's flash."};
}

CommandResult DemoProfileGateway::flashSlotProfile(const std::uint8_t slot) {
    simulateLatency();
    const std::size_t index = slot % SLOT_COUNT;
    if (!m_slots[index].hasProfile) return {false, "No profile file is assigned to " + slotLabel(index) + "."};

    m_hardware[index] = m_slots[index].storedSettings.value_or(ActuationSettings{});
    return {true, "'" + m_slots[index].name + "' was sent to " + slotLabel(index) + "."};
}

void DemoProfileGateway::handleFileCreated(const std::filesystem::path&) {}
void DemoProfileGateway::handleFileDeleted(const std::filesystem::path&) {}
void DemoProfileGateway::handleDeviceConnected(kbd::TransportMode, const std::string&) {}
void DemoProfileGateway::handleDeviceDisconnected() {}

} // namespace ui
