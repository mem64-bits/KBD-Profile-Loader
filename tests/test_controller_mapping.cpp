#include <gtest/gtest.h>

#include "kbd/GameController.h"
#include "kbd/KeychronProtocol.h"
#include "kbd/ProfileMemory.h"
#include "ui/AppState.h"
#include "ui/ControllerMapping.h"
#include "ui/KeyboardLayout.h"

#include <set>
#include <vector>

using kbd::KeychronProtocol;
namespace mapping = ui::mapping;

// ---------------------------------------------------------------------------
// Game-controller targets and presets
// ---------------------------------------------------------------------------

TEST(ControllerMappingTest, EveryTargetIsValidUniqueAndNamed) {
    std::set<std::uint8_t> seen;
    for (const mapping::TargetInfo& info : mapping::targets()) {
        EXPECT_TRUE(kbd::GamepadTarget::isValid(info.target)) << info.name;
        EXPECT_TRUE(seen.insert(info.target).second) << "duplicate target " << static_cast<int>(info.target);
        EXPECT_NE(info.name[0], '\0');
        EXPECT_NE(info.chip[0], '\0');
    }
    // 10 analog directions + 2 dial halves + 32 buttons
    EXPECT_EQ(seen.size(), 44u);
    EXPECT_FALSE(kbd::GamepadTarget::isValid(12)) << "GC_MAX is not a target";
    EXPECT_FALSE(kbd::GamepadTarget::isValid(kbd::GamepadTarget::END));
}

TEST(ControllerMappingTest, TargetsMatchTheFirmwaresXInputOrder) {
    // Taken from a real Keychron Launcher export (tests/fixtures/launcher/HEProfile-Gamepad-*.json): A = 0, D = 1,
    // S = 2, W = 3, '/' = 13 (button A), Home = 27 (Guide).
    EXPECT_EQ(mapping::targetInfo(3)->axis, ui::gamepad::Axis::LeftY);
    EXPECT_EQ(mapping::targetInfo(3)->axisSign, +1);
    EXPECT_EQ(mapping::targetInfo(0)->axisSign, -1);
    EXPECT_EQ(mapping::targetInfo(13)->button, ui::gamepad::Button::South);
    EXPECT_EQ(mapping::targetInfo(27)->button, ui::gamepad::Button::Guide);
    EXPECT_EQ(mapping::targetInfo(23)->button, ui::gamepad::Button::DpadUp);
    EXPECT_EQ(mapping::targetName(13, ui::gamepad::LabelStyle::PlayStation), "Cross");
    EXPECT_EQ(mapping::targetName(5, ui::gamepad::LabelStyle::PlayStation), "R2");
    EXPECT_EQ(mapping::targetInfo(99), nullptr);
}

TEST(ControllerMappingTest, PresetsOnlyUseRealKeysAndTargets) {
    for (const mapping::Preset& preset : mapping::presets()) {
        std::set<ui::KeyPosition> keys;
        for (const auto& [key, target] : preset.bindings) {
            EXPECT_TRUE(ui::isKeyOnBoard(key)) << preset.name;
            EXPECT_NE(mapping::targetInfo(target), nullptr) << preset.name;
            EXPECT_TRUE(keys.insert(key).second) << preset.name << ": a key is bound twice";
        }
    }
    const auto& full = mapping::presets()[3];
    EXPECT_STREQ(full.name, "Full controller");
    EXPECT_EQ(full.bindings.size(), 25u) << "every stick direction, trigger and XInput button";
}

TEST(ControllerMappingTest, DefaultKeycodesFollowTheLayout) {
    using ui::LayoutKind;
    EXPECT_EQ(mapping::defaultKeycode(LayoutKind::Iso, {2, 2}), 0x1A);    // W
    EXPECT_EQ(mapping::defaultKeycode(LayoutKind::Iso, {2, 13}), 0x28);   // ISO: Enter sits in row 2
    EXPECT_EQ(mapping::defaultKeycode(LayoutKind::Ansi, {2, 13}), 0x31);  // ANSI: backslash
    EXPECT_EQ(mapping::defaultKeycode(LayoutKind::Iso, {4, 13}), kbd::Keycode::UP);
    EXPECT_FALSE(mapping::defaultKeycode(LayoutKind::Iso, {5, 10})) << "Fn has no keycode";
    EXPECT_EQ(mapping::keycodeName(kbd::Keycode::LEFT_SHIFT), "Left Shift");
    EXPECT_EQ(mapping::keycodeName(0x1A), "W");
}

TEST(ControllerMappingTest, SocdModesCoverTheFirmwareTypes) {
    std::set<std::uint8_t> types;
    for (const auto& mode : mapping::socdModes()) types.insert(mode.type);
    EXPECT_EQ(types, (std::set<std::uint8_t>{1, 2, 3, 4, 5, 6}));
    EXPECT_STREQ(mapping::socdMode(6)->name, "Neutral");
    EXPECT_EQ(mapping::socdMode(0), nullptr);
}

// ---------------------------------------------------------------------------
// Response curve
// ---------------------------------------------------------------------------

TEST(ResponseCurveTest, OutputFollowsTheFirmwaresPiecewiseLine) {
    const kbd::ResponseCurve& curve = kbd::DEFAULT_CURVE;
    EXPECT_FLOAT_EQ(kbd::curveOutput(curve, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(kbd::curveOutput(curve, 10.0f), 31.0f);
    EXPECT_FLOAT_EQ(kbd::curveOutput(curve, 20.0f), 63.0f);
    EXPECT_FLOAT_EQ(kbd::curveOutput(curve, 40.0f), 127.0f);
    EXPECT_FLOAT_EQ(kbd::curveOutput(curve, 55.0f), 127.0f);

    const kbd::ResponseCurve deadzone{{{4, 0}, {10, 60}, {20, 100}, {30, 127}}};
    EXPECT_FLOAT_EQ(kbd::curveOutput(deadzone, 3.0f), 0.0f) << "before the first point the stick stays centred";
    EXPECT_FLOAT_EQ(kbd::curveOutput(deadzone, 7.0f), 30.0f);
}

TEST(ResponseCurveTest, OnlyOrderedInRangeCurvesAreValid) {
    EXPECT_TRUE(kbd::isValidCurve(kbd::DEFAULT_CURVE));
    EXPECT_TRUE(kbd::isValidCurve({{{1, 0}, {4, 127}, {4, 127}, {4, 127}}})) << "equal x is allowed";
    EXPECT_FALSE(kbd::isValidCurve({{{0, 0}, {20, 31}, {10, 95}, {40, 127}}}));
    EXPECT_FALSE(kbd::isValidCurve({{{0, 0}, {10, 31}, {30, 128}, {40, 127}}}));
    EXPECT_FALSE(kbd::isValidCurve({{{0, 0}, {10, 31}, {30, 95}, {41, 127}}}));
}

// ---------------------------------------------------------------------------
// Packets
// ---------------------------------------------------------------------------

TEST(ControllerProtocolTest, CurvePacketsAndReplies) {
    const auto packet = KeychronProtocol::createCurvePacket(kbd::DEFAULT_CURVE);
    ASSERT_EQ(packet.size(), 32u);
    EXPECT_EQ(packet[0], 0xA9);
    EXPECT_EQ(packet[1], 0x21);
    EXPECT_EQ(std::vector<std::uint8_t>(packet.begin() + 2, packet.begin() + 10), (std::vector<std::uint8_t>{0, 0, 10, 31, 30, 95, 40, 127}));
    EXPECT_THROW((void)KeychronProtocol::createCurvePacket({{{0, 0}, {20, 31}, {10, 95}, {40, 127}}}), std::invalid_argument);

    std::vector<std::uint8_t> reply(32, 0);
    reply[0] = 0xA9;
    reply[1] = 0x20;
    const std::uint8_t points[] = {2, 0, 16, 20, 32, 70, 38, 127};
    std::copy(std::begin(points), std::end(points), reply.begin() + 2);
    const auto curve = KeychronProtocol::parseCurve(reply);
    ASSERT_TRUE(curve);
    EXPECT_EQ((*curve)[2], (kbd::CurvePoint{32, 70}));
    reply[1] = 0x21;
    EXPECT_FALSE(KeychronProtocol::parseCurve(reply)) << "only a GET_CURVE answer counts";
}

TEST(ControllerProtocolTest, ControllerModePacketsKeepOnlyKnownBits) {
    const auto set = KeychronProtocol::createControllerModePacket(0xFF);
    EXPECT_EQ(set[1], 0x23);
    EXPECT_EQ(set[2], kbd::ControllerMode::XINPUT | kbd::ControllerMode::TYPING);
    EXPECT_EQ(KeychronProtocol::createControllerModeReadPacket()[1], 0x22);

    std::vector<std::uint8_t> reply(32, 0);
    reply[0] = 0xA9;
    reply[1] = 0x22;
    reply[3] = 0x03;
    EXPECT_EQ(KeychronProtocol::parseControllerMode(reply), 0x03);
    reply[2] = 1; // firmware failure flag
    EXPECT_FALSE(KeychronProtocol::parseControllerMode(reply));
}

TEST(ControllerProtocolTest, RealtimeTravelRepliesMustMatchTheKey) {
    std::vector<std::uint8_t> reply(32, 0);
    reply[0] = 0xA9;
    reply[1] = 0x30;
    reply[2] = 0;    // ok
    reply[3] = 2;    // row
    reply[4] = 2;    // col: W
    reply[5] = 27;   // 2.7 mm
    reply[6] = 162;  // 27 * 6
    reply[7] = 0x34; reply[8] = 0x12;
    reply[13] = 0x11;
    const auto travel = KeychronProtocol::parseRealtimeTravel(reply, 2, 2);
    ASSERT_TRUE(travel);
    EXPECT_EQ(travel->travelTenths, 27);
    EXPECT_EQ(travel->travelRaw, 162);
    EXPECT_EQ(travel->sensorValue, 0x1234);
    EXPECT_TRUE(travel->pressed);
    EXPECT_FALSE(KeychronProtocol::parseRealtimeTravel(reply, 3, 2)) << "an answer for another key";
    reply[2] = 1;
    EXPECT_FALSE(KeychronProtocol::parseRealtimeTravel(reply, 2, 2)) << "the firmware refused the request";
}

// ---------------------------------------------------------------------------
// Dynamic keystrokes (OKMC)
// ---------------------------------------------------------------------------

TEST(DynamicKeystrokeTest, EntryPacksTravelIntoSixBitFieldsAndActionsIntoNibbles) {
    const kbd::OkmcConfig sprint = kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 10, 30);
    EXPECT_EQ(sprint.shallowActTenths, 10);
    EXPECT_EQ(sprint.deepActTenths, 30);
    EXPECT_EQ(sprint.deepDeactTenths, 27);

    const auto bytes = kbd::ProfileMemory::encodeOkmcEntry(sprint);
    // 10 | 10 << 6 | 30 << 12 | 27 << 18 = 0x6DE28A, little-endian.
    EXPECT_EQ(bytes[0], 0x8A);
    EXPECT_EQ(bytes[1], 0xE2);
    EXPECT_EQ(bytes[2], 0x6D);
    EXPECT_EQ(bytes[3], 0x1A); // W
    EXPECT_EQ(bytes[4], 0x00);
    EXPECT_EQ(bytes[5], 0xE1); // Left Shift
    EXPECT_EQ(bytes[11], 0x12); // W: shallow press (2), shallow release (1) << 4
    EXPECT_EQ(bytes[12], 0x00);
    EXPECT_EQ(bytes[13], 0x10); // Shift: released with the key
    EXPECT_EQ(bytes[14], 0x12); // Shift: deep press, deep release

    EXPECT_EQ(kbd::ProfileMemory::decodeOkmcEntry(bytes), sprint);
    EXPECT_TRUE(mapping::isDeepPressCombo(sprint));
    EXPECT_FALSE(mapping::isDeepPressCombo(kbd::OkmcConfig{}));
}

TEST(DynamicKeystrokeTest, ComboKeepsThePointsOrderedAndInRange) {
    const auto clamped = kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 0, 0);
    EXPECT_GE(clamped.shallowActTenths, 1);
    EXPECT_GT(clamped.deepActTenths, clamped.shallowActTenths);
    EXPECT_GE(clamped.deepDeactTenths, clamped.shallowActTenths);
    EXPECT_LT(clamped.deepDeactTenths, clamped.deepActTenths);
}

TEST(DynamicKeystrokeTest, PacketCarriesTheEntryAfterTheKey) {
    const kbd::OkmcConfig sprint = kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 12, 32);
    const auto packet = KeychronProtocol::createOkmcPacket(4, 2, 2, 7, sprint);
    ASSERT_EQ(packet.size(), 32u);
    EXPECT_EQ(packet[1], 0x15);
    EXPECT_EQ(packet[2], 1) << "slot index wraps at 3";
    EXPECT_EQ(packet[3], KeychronProtocol::ADV_MODE_OKMC);
    EXPECT_EQ(packet[4], 2);
    EXPECT_EQ(packet[5], 2);
    EXPECT_EQ(packet[6], 7);
    EXPECT_EQ(packet[7], 12);
    EXPECT_EQ(packet[8], 12);
    EXPECT_EQ(packet[9], 32);
    EXPECT_EQ(packet[10], 29);
    EXPECT_EQ(packet[11], 0x1A);
    EXPECT_EQ(packet[13], 0xE1);
    EXPECT_EQ(packet[19], 0x12);
    EXPECT_EQ(packet[22], 0x12);
    EXPECT_EQ(packet[27], 0) << "nothing past the 8 action bytes";
}

TEST(DynamicKeystrokeTest, SlotDetailsDecodeAndHandOutFreeEntries) {
    std::vector<std::uint8_t> memory(kbd::ProfileMemory::PROFILE_SIZE, 0);
    kbd::KeyTravelConfig w;
    w.mode = 0;
    w.advancedMode = kbd::AdvancedMode::DKS;
    w.advancedData = 0;
    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(memory, 2, 2, w));
    ASSERT_TRUE(kbd::ProfileMemory::encodeOkmc(memory, 0, kbd::deepPressCombo(kbd::Keycode::W, kbd::Keycode::LEFT_SHIFT, 10, 30)));
    kbd::KeyTravelConfig a;
    a.mode = 0;
    a.advancedMode = kbd::AdvancedMode::GAMEPAD;
    a.advancedData = kbd::GamepadTarget::LEFT_STICK_LEFT;
    ASSERT_TRUE(kbd::ProfileMemory::encodeKeyTravel(memory, 3, 1, a));
    ASSERT_TRUE(kbd::ProfileMemory::encodeSocd(memory, 4, kbd::SocdPair{3, 1, 3, 3, 3}));

    const ui::SlotDetails details = ui::SlotDetails::fromMemory(memory);
    EXPECT_EQ(details.at({2, 2}), (ui::KeyAdvanced{kbd::AdvancedMode::DKS, 0}));
    EXPECT_EQ(details.at({3, 1}), (ui::KeyAdvanced{kbd::AdvancedMode::GAMEPAD, 0}));
    EXPECT_TRUE(mapping::isDeepPressCombo(details.okmc[0]));
    ASSERT_EQ(details.socdPairs().size(), 1u);
    EXPECT_EQ(details.socdPairs()[0].type, 3);

    EXPECT_EQ(details.okmcIndexFor({2, 2}), 0) << "W keeps its own entry";
    EXPECT_EQ(details.okmcIndexFor({2, 3}), 1) << "E gets the first unused one";
}
