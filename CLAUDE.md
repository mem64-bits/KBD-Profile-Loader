# CLAUDE.md - KBD-Profile-Loader

Directives and context for AI assistants working in this repo. Keep this file current when
something here stops being true.

## What this is

C++20 tray app for the Keychron K2 HE (Hall-effect keyboard, Keychron's open-source QMK firmware).
Switches between profile `.json` files and tunes actuation / Rapid Trigger, SOCD, sprint-on-deep-press
(dynamic keystrokes) and the built-in analog game controller. Dear ImGui 1.93 WIP (dynamic fonts),
SDL3 3.4.20 (window, OpenGL context, input, game controllers; FetchContent, static) + OpenGL,
hidapi, nlohmann/json, GoogleTest via FetchContent. Targets Windows (MSVC / CLion) and Linux
(X11 + Wayland). Main dev machine is Windows.

## Decisions (user, 2026-11-04)

- Qt is out (too big). ImGui stays the UI toolkit; SDL3 replaced GLFW (one platform layer for the
  window and game controllers). The tray stays our own (`Win32TrayIcon`, `SniTrayIcon`): SDL's tray
  uses AppIndicator/GTK on Linux.
- Wired only: the raw-HID configuration interface is used over USB-C (PIDs 0x0E20 ANSI, 0x0E21 ISO,
  0x0E22 JIS). No 2.4 GHz dongle / Bluetooth detection.
- Lighting is left to OpenRGB 1.0 (it speaks Keychron's 0xA8 protocol) and the Launcher; macros and
  firmware flashing stay in the Keychron Launcher.
- Game controller: use the keyboard's own XInput output; no virtual-controller driver (ViGEmBus is
  retired). Sprint: firmware dynamic keystroke (W, + Shift on a deep press) and/or WASD as an analog
  left stick. Gamepad page: live view, key mapper + presets, response curve, fighting-game input
  history. Navigation: left sidebar.
- Round 4 (2026-11-04): profile-centric layout (sidebar Library / Keyboard / Settings; the Keyboard
  page holds Actuation, Movement and Gamepad as tabs under the slot switcher). Gamepad mode follows
  the active slot automatically (XInput on when the slot has gamepad keys, off when it has none;
  toggle on the Gamepad tab). ImPlot for diagrams. Controller art must be properly licensed:
  input-overlay's CC0 "xbox-controller" drawing (Xbox logo removed) and Kenney's CC0 Input Prompts
  glyphs, plus Zacksly's "Xbox Series" and "PS5 Button Icons and Controls" (CC BY 3.0, packed in
  assets/glyphs/zacksly-prompts.png: the credit in Settings and
  README is required, and must say the icons were modified). Prompt style is chosen in Settings.
  The downloaded pack folder and zip stay out of git (.gitignore); only the packed atlas is committed.
- The Keychron Launcher is NOT open source (only the firmware and the case CAD files are published).
  The JS dump stays a reference for protocol facts only; never copy its code or artwork.

## Build and test

- Submodules live in `external/` (also gitignored): `git submodule update --init`.
- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build`
- `ctest --test-dir build --output-on-failure`
- UI without hardware: `KBD_Profile_Loader --demo`; headless check:
  `--demo --screenshot out.png --tab actuation` (tabs: profiles, actuation, movement, gamepad,
  gamepad-mapping, gamepad-curve, settings; `--slot 1-3` picks the demo's active slot).
- SDL3 is downloaded at configure time (pinned URL + SHA-256). Offline:
  `-DFETCHCONTENT_SOURCE_DIR_SDL3=<unpacked SDL3-3.4.20>`. Linux needs the usual X11 / Wayland -dev
  packages; XScrnSaver and XTest are switched off in `CMakeLists.txt`.
- After adding or removing source files run `python3 sync_cmake.py` (refreshes CMake file lists).
- `CMakeLists.txt` uses CRLF line endings; keep them, or git shows the whole file as changed.

## Architecture rules

- Namespaces: `ui` (UI), `kbd` (hardware), `Files` (files and logging).
- The UI never touches HID. Flow:
  ImGui panels -> `IProfileCommands` (`ProfileController`: one worker thread + queue)
  -> `IProfileGateway` (`ProfileServiceGateway` | `DemoProfileGateway`) -> `kbd::ProfileService`
  -> `KeyboardDevice`. Results come back as an immutable `AppSnapshot` through `AppModel`.
- Panel state that belongs to a slot or profile goes in `AppSnapshot` / the gateway, not in
  file-scope statics inside a panel.
- Every new method on `IProfileGateway` or `IProfileCommands` must also be implemented in
  `DemoProfileGateway`, `tests/support/FakeProfileGateway.h` and `tests/support/Fakes.h`
  (`FakeProfileCommands`), or the test target stops compiling.
- pimpl for anything that would pull `windows.h`, SDL or GIO into a header; special members
  spelled out (rule of five); platform code only in `*Win32*.cpp`, `*Posix*.cpp`, `SniTrayIcon.cpp`.
- When the keyboard's read-back disagrees with what was sent, the status line must say so.
  Never report success to hide a mismatch (see `docs/FIRMWARE_NOTES.md`).
- ImGui 1.92+ font system: size comes from `style.FontSizeBase` and DPI from `style.FontScaleDpi`
  (set in `ImGuiTheme.cpp`). Do not also bake DPI into the atlas or set `ImFont::Scale`.
- SDL lives only in `src/ui/imgui/` (`UIManager`, `SdlGamepadSource`); everything below it is
  SDL-free and unit-tested (`GamepadModel`, `ControllerMapping`, `DemoGamepadSource`). Game-controller
  input goes through `ui::gamepad::IGamepadSource`; the SDL source only listens while the window is
  shown (`setActive`) and must never be opened by the ImGui backend (`GamepadMode_Manual`).
- Other programs (Launcher, OpenRGB) get copies of every raw-HID reply: always match the reply's
  `[0]`/`[1]` against the command sent (`awaitResponse`, `queryActiveHardwareSlot`).
- Live key travel (`watchLiveTravel`) is polled by the controller's worker at ~60 Hz only while the
  window is open and a page watches keys; pages stop it in `IPanel::onHidden`.
- ImGui drag and drop: one payload per drag (a second `SetDragDropPayload` replaces the first);
  `BeginDragDropSource` / `BeginDragDropTarget` act on the last item, so call the target after
  `EndChild()` and give the source a real item (button / selectable) to grab.

## Firmware facts

Checked 2026-11-04 against Keychron/qmk_firmware branch `2025q3`
(`keyboards/keychron/common/analog_matrix/{analog_matrix_type.h,analog_matrix.c,profile.c}`)
and the Launcher web bundle in `keychron_launcher_dump/`.

- `analog_matrix_profile_t` is packed, 860 bytes for the 6 x 16 matrix:

  | offset | field | size |
  |---|---|---|
  | 0 | `global` (`analog_key_config_t`) | 4 |
  | 4 | `key_config[6][16]` (`analog_key_config_t`) | 384 |
  | 388 | `okmc[20]` (dynamic keystrokes, 19 bytes each) | 380 |
  | 768 | `socd[20]` (3 bytes each) | 60 |
  | 828 | `name[30]` | 30 |
  | 858 | `crc16` | 2 |

  A per-key entry has the same 4-byte bitfield layout as `global` (`kbd::ProfileMemory`
  has `keyConfigOffset`, `decodeKeyTravel`, `encodeKeyTravel`, `resolveKeyTravel`).
  Key (r, c) starts at `4 + (r * 16 + c) * 4`. OKMC_COUNT = SOCD_COUNT = 20 and
  PROFILE_NAME_LEN = 30 are defined in `analog_matrix_eeconfig.h`.
  Per-key 0 in any field = use the global value (`update_key_config`).
- `0x12` is `AMC_GET_PROFILE_RAW`, a read (`[2]` slot, `[3..4]` offset LE, `[5]` size <= 26,
  reply bytes at `[6..]`). There is no raw-write command. Writing a profile means replaying
  setters (`0x14` travel, `0x15` advance mode, `0x16` SOCD, `0x13` name) and then `0x1F` save;
  `ProfileServiceGateway::writeProfile` does travel, advanced key modes (`0x15`, with the OKMC
  entry for dynamic keystrokes), SOCD (`0x16`), name and save.
- `0x13` name: `[3]` = length (max 28), text from `[4]`.
- `0x14 AMC_SET_TRAVAL` request:
  `[0]=0xA9 [1]=0x14 [2]=profile [3]=mode [4]=act_pt [5]=press sens [6]=release sens [7]=entire`
  - mode: `0` follow global (per-key only), `1` regular, `2` Rapid Trigger.
  - act_pt 0-39 (0.1 mm units; 0 only per key = follow global).
  - `entire = 1`: whole keyboard. `entire = 0`: per-key masks follow at `[8..25]`, 3 bytes per
    row, little-endian, bit `c` = column `c`.
  - Reply `[2]` = 0 on success.
  - Per-key changes on the active profile apply at once (`update_key_config`); global changes
    only after `profile_select`.
- Launcher JSON export (real samples in `tests/fixtures/launcher/`, tested by
  `tests/test_launcher_profiles.cpp`): no name inside (taken from `HEProfile-<name>-<date>.json`),
  `current` = slot, `global` + `keyMatrix[row][col]` {mode, distance, advance_press,
  advance_release, advanceMode, dksIndex, dirty, edit}, `snapTap` SOCD pairs. For keys that follow
  the slot the top-level distances are display values; read the `edit` copy (raw) unless `dirty`.
- `0x15` advanced mode: `[3]` 0 clear / 1 OKMC / 2 game controller / 3 toggle, `[4]` row, `[5]` col,
  `[6]` index. For OKMC `[7..10]` shallow act / shallow deact / deep act / deep deact (0.1 mm),
  `[11..18]` 4 keycodes LE, `[19..26]` 4 actions (2 bytes: low nibble first; 1 release, 2 press,
  6 tap, 7 re-tap). Applies at once on the active slot (`update_key_config`).
  `0x16` SOCD: `[3..6]` row1 col1 row2 col2, `[7]` index 0-19, `[8]` type (0 clears; 1/2 deeper
  press wins, 3 last input, 4/5 key 1/2 wins, 6 neutral). The Launcher shows at most 5 pairs.
- Game controller (keyboard-wide, EEPROM at once, no save command): `0x20`/`0x21` response curve,
  4 points (x 0-40 in 0.1 mm, y 0-127) at `[2..9]`; the firmware validates the OLD curve, so never
  send an unordered one (`kbd::isValidCurve`). `0x22`/`0x23` mode bits: 0x01 XInput (else HID
  joystick), 0x02 gamepad keys also type; takes effect after re-plugging. Gamepad target index
  (`key_config.js_axis`): 0-11 axis halves (LS left/right/down/up, LT, RT, RS ..., dial), 13+ buttons
  in XInput order A B X Y LB RB Back Start LS RS Up Down Left Right Guide.
- `0x30` live travel of one key: request `[2]` row `[3]` col; reply `[2]` status, `[5]` 0.1 mm,
  `[6]` 1/60 mm, `[7..12]` raw / zero / full sensor values, `[13]` 0x11 pressed.
- Profiles are identified by file name (`KeyboardProfile::operator==`), not by display name.
- Only the 85 matrix positions that exist on the board count as keys (`ui::isKeyOnBoard`).

## Keyboard drawing

- Geometry lives in `src/ui/KeyboardLayout.cpp` (ISO 85 keys, ANSI 84), generated from Keychron's
  QMK `keyboards/keychron/k2_he/{iso,ansi}/keyboard.json` (x, y, w, h, matrix) and
  `via_json/*.json` (ISO Enter shape, keycap colour family). `tests/test_keyboard_layout.cpp`
  checks it (unique positions, no overlaps, rows 16U wide).
- `ui::KeyboardView` draws keycaps with the ImGui draw list only. Do not use Keychron Launcher
  code or artwork (the PNGs / JS in `keychron_launcher_dump/`): it is proprietary and the repo is
  public. Using protocol facts learned from it is fine.

## Repo hygiene

- Do not commit `keychron_launcher_dump/` or `keychron_launcher_assets.zip`: Keychron's own
  web bundle, kept locally for reference only.
- `CMakeLists.txt.bak` and `KBD-Profile-Loader-changes.patch` are tracked but are leftovers;
  `sync_cmake.py` rewrites `CMakeLists.txt.bak` every time it runs.
- `assets/layouts/k2_he_iso_uk.json` is no longer used (the layout is compiled in).

## State (2026-11-04, round 4)

Round 4: `KeyboardPage` (tabs), shared `Workspace` (picked slot, layout, label style, glyph style),
`kbd::model` (FirmwareModel: exact ports of the firmware's XInput, SOCD and trigger logic, tested in
test_firmware_model.cpp) driving the SOCD test bench, the key preview and the travel diagrams, ImPlot
plots (`Plots.cpp`), CC0 controller drawing (`ControllerArt.cpp`, atlas embedded by CMake) and Kenney
glyphs (`Glyphs.cpp`, `assets/glyphs/kenney-input-prompts.png`: row 0 Xbox, row 1 PlayStation, 24
columns in the order listed in Glyphs.cpp). `ProfileController::syncControllerMode` runs after every
slot switch (app, tray, hotkey, Fn + P seen by the slot poll), never on the first slot seen after start-up or
a reconnect. The replug banner is a cross-check, not a flag: `ControllerSettings::replugNeeded` = stored mode's
XInput bit != `KeyboardDevice::enumeratedAsXInput()` (the firmware cuts the joystick collection, usage page 1
usage 4, from its shared HID interface in XInput mode, so the HID device list shows what the PC enumerated).
The firmware cannot re-enumerate itself (Keychron commented out usbDisconnectBus / usb_start in
game_controller_mode_set); only a replug or a PC-side USB restart switches the running mode. `widgets::segmented` is a pill track whose items
never cut their labels; use tables, not BeginGroup + SameLine, for two-column cards (right-aligned
values otherwise run to the window edge).

DualSense drawing (`DualSenseArt.cpp`, `assets/controller/dualsense-atlas.png`, CC0 input-overlay preset,
logos erased by `tools/make_dualsense_atlas.py`) replaces the Xbox drawing when PlayStation names are chosen
(`art::modelFor`). Same `art::Part`s, so mapping / hit-testing code is shared.

Open: everything in round 3's list below, and making the demo simulate the replug. The Zacksly
controller drawings (full pad images) are unused: ControllerArt's part positions are calibrated to the
input-overlay drawing.

## State (2026-11-04, round 3)

Round 3: SDL3 instead of GLFW, wired-only detection with presence checks (no ping packets), sidebar
navigation, Movement page (SOCD editor, sprint on deep press), Gamepad page (SDL3 live view with
trails / deadzone / circularity, input history in pad / Tekken / SF notation, key mapper with
presets, controller mode, response curve editor with live key travel), dynamic keystrokes written
with profiles, reply matching for the active-slot query.

Open: real-keyboard test of round 3 (XInput detection through SDL, live travel rate, OKMC), MSVC
build, exporting a slot back into a profile file (edits made on the keyboard are not saved to the
file), the Launcher's `dks` JSON format (unknown: the user's exports have none), persisting the
library order, `git rm external/glfw` (submodule no longer used).

## State before round 3

Done: profile library (assign / unassign / reorder, one file per slot), per-key actuation end to
end (`IProfileCommands::applyKeyActuation`, per-key read-back via `readKeyMatrix`), profile
writing by setter replay, keyboard view, protocol fixes listed in `docs/FIRMWARE_NOTES.md`.

Writing a profile also sends gamepad / toggle keys and SOCD pairs (round 2, after checking the
user's real Launcher exports).

(Round 2 open items, now done except persisting the library order and the hardware test.)
