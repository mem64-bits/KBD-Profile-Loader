# Firmware notes

The K2 HE runs Keychron's open-source QMK firmware (<https://github.com/Keychron/qmk_firmware>). This
document records what the app relies on and how it was checked.

Sources read: branch `2025q3` (files `keyboards/keychron/common/analog_matrix/{analog_matrix.c,profile.c,
analog_matrix_type.h,analog_matrix_eeconfig.h}`, `keyboards/keychron/k2_he/{iso,ansi}/keyboard.json` and
`keyboards/keychron/k2_he/via_json/*.json`), last checked 2026-11-04, plus the request / reply handling in
the Keychron Launcher web app's JavaScript (which sends the same commands).

**Not yet tested on a keyboard.** The shipping firmware could differ from the public source; the UI reads every
change back and reports any difference in the status line.

## Profile layout

Every slot holds one `analog_matrix_profile_t` (packed, 860 bytes on the K2 HE's 6 x 16 matrix). The
`memory` / `raw_hex` dumps in profile files use the same layout; `kbd::ProfileMemory` holds the offsets.

| Offset | Field | Size |
|---|---|---|
| 0 | `global` (`analog_key_config_t`) | 4 |
| 4 | `key_config[6][16]`: key (row, col) at `4 + (row * 16 + col) * 4` | 384 |
| 388 | `okmc[20]` (dynamic keystrokes, 19 bytes each) | 380 |
| 768 | `socd[20]` (3 bytes each) | 60 |
| 828 | `name[30]` | 30 |
| 858 | `crc16` | 2 |

`analog_key_config_t` packs mode (2 bits), actuation point (6), press sensitivity (6), release sensitivity (6),
advanced mode (4) and advanced data (8); the bit layout was checked by compiling the firmware's own struct
(`tests/test_profile_memory.cpp`). In a per-key entry every field that is 0 means "use the global value"
(`analog_matrix.c: update_key_config`); a release sensitivity of 0 means "same as press".

## Commands the app uses (`0xA9` = analog matrix)

| Command | Request | Reply |
|---|---|---|
| `0x10` profile info | - | `[2]` active slot, `[3]` slot count, `[4..5]` profile size, `[6]` OKMC count, `[7]` SOCD count |
| `0x11` select slot | `[2]` slot | `[2]` 0 = ok |
| `0x12` read profile | `[2]` slot, `[3..4]` offset (LE), `[5]` size (max 26) | the bytes at `[6..]` |
| `0x13` set name | `[2]` slot, `[3]` length (max 28), `[4..]` text | `[2]` 0 = ok |
| `0x14` set travel | `[2]` slot, `[3]` mode, `[4]` actuation, `[5]` press, `[6]` release, `[7]` 1 = whole keyboard, `[8..25]` key masks | `[2]` 0 = ok |
| `0x15` set advanced mode | `[2]` slot, `[3]` 0 clear / 1 dynamic keystroke / 2 game controller / 3 toggle, `[4]` row, `[5]` col, `[6]` index (axis / DKS slot); for a dynamic keystroke also `[7..10]` travel points, `[11..18]` keycodes, `[19..26]` actions | `[2]` 0 = ok |
| `0x16` set SOCD | `[2]` slot, `[3]` row 1, `[4]` col 1, `[5]` row 2, `[6]` col 2, `[7]` index (0 - 19), `[8]` type (0 clears, 1 - 6) | `[2]` 0 = ok |
| `0x1F` save | `[2]` slot | `[2]` 0 = ok |
| `0x20` get curve | - | 4 points (x, y) at `[2..9]`, no status byte |
| `0x21` set curve | 4 points at `[2..9]`; x 0 - 40 (0.1 mm), y 0 - 127 | `[2]` 0 = ok (written to EEPROM at once) |
| `0x22` get controller mode | - | `[2]` 0 = ok, `[3]` mode bits |
| `0x23` set controller mode | `[2]` bits: 0x01 XInput, 0x02 gamepad keys also type | `[2]` echoes the mode (no status); EEPROM at once, active after re-plugging |
| `0x30` live travel | `[2]` row, `[3]` col | `[2]` 0 = ok, `[3..4]` row / col, `[5]` travel 0.1 mm, `[6]` travel 1/60 mm, `[7..12]` raw / zero / full sensor values, `[13]` 0x11 pressed |

Other programs talking to the same raw-HID interface (the Launcher, OpenRGB's Keychron controller) receive
copies of every reply and so does this app, so replies are always matched by `[0]` / `[1]` (and by the echoed
key / slot / offset where the reply has one).

### Dynamic keystrokes (`okmc_config_t`, 19 bytes, `action_okmc.c`)

Bytes 0 - 2: four packed 6-bit travel points, least significant first (shallow actuate, shallow release, deep
actuate, deep release, 0.1 mm). Bytes 3 - 10: four keycodes (QMK basic keycodes / modifiers, little-endian).
Bytes 11 - 18: one `okmc_action_t` per keycode, two nibbles per byte, low nibble first: shallow press, shallow
release, deep press, deep release. An action nibble is run bit by bit (bit 0 release, bit 1 press, bit 2 release),
so 1 = release, 2 = press, 6 = tap, 7 = re-tap. The deep point only re-arms after the key comes back up past the
shallow release point. "Sprint on deep press" = keycode 0 (W) pressed at the shallow point and released at the
end, keycode 1 (Shift) pressed at the deep point and released at the deep release point (0.3 mm higher) or at the end.

### Game controller (`game_controller_common.c`, `action_xinput.c`)

A key in gamepad mode drives `key_config.js_axis`: 0 / 1 left stick left / right, 2 / 3 down / up, 4 / 5 left /
right trigger, 6 - 9 the right stick the same way, 10 / 11 a dial (HID joystick only), 13 + n button n. In XInput
mode buttons 0 - 14 are A, B, X, Y, LB, RB, Back, Start, left / right stick click, D-pad up / down / left / right,
Guide (checked against the user's Launcher export, where `/`, `'`, `;`, `[` are 13 - 16). Stick values follow the
4-point curve (piecewise linear, 0 until the first point); triggers ignore it. The firmware's set-curve check
looks at the curve it already has, not the new one, so the app validates curves itself. XInput mode is selected
with the MS OS string descriptor at enumeration, which is why a mode change needs a re-plug.

`0x14` details (`analog_matrix.c` / `profile.c: profile_set_traval`):

- mode: `0` follow global (per key only; rejected for the whole keyboard), `1` regular, `2` Rapid Trigger.
- actuation 0 - 39 (0.1 mm); 0 = follow global for a key.
- With `[7] = 0`, row `r`'s mask is 3 bytes at `[8 + 3r]`, copied into a little-endian `uint32_t`; bit `c` selects
  column `c`. One packet sets every selected key to the same values, so the app groups keys by value.
- Per-key changes to the active slot take effect at once (`update_key_config`). Whole-keyboard changes only
  reach the key scanner when the active profile changes, so after editing the active slot the app switches to a
  neighbouring slot and back.

## What was wrong before, and what changed

1. **There is no raw-profile write.** `0x12` only reads. The old *Write to keyboard* streamed 34 `0x12` packets,
   which changed nothing. **Now** a profile is written the way the Launcher does it: global `0x14`, one per-key
   `0x14` per group of keys with the same values (keys that follow the slot included, so old overrides are
   cleared), `0x15` for every key on the board (game-controller axes and toggle keys set, everything else
   cleared, so nothing survives from the slot's previous profile), all 20 SOCD entries with `0x16`, `0x13` name,
   then `0x1F`. Dynamic keystrokes (DKS) are not written yet; the status line says so when a file has them.
2. **Rapid Trigger mode byte.** `createFastTravelPacket` sent `1` for "Rapid Trigger on" (= regular) and `0` for
   off (rejected). **Fixed**: `1` regular, `2` Rapid Trigger.
3. **Profile name.** `createSetProfileNamePacket` left out the length byte. **Fixed**.
4. **Launcher JSON.** The parser put the name at bytes 4 - 31 (over the first keys) and the per-key table at
   offset 92 with 8 bytes per key. **Fixed** to the layout above.
5. **SOCD.** `createSocdPairPacket` used `0xA8 / 0x09`, which the firmware does not handle; it was replaced by
   `createSocdPacket` (`0xA9 0x16`, see the table).
6. **Opcode naming**: `AMC_CLEAR_PROFILE = 0x1D` in `KeychronProtocol.h`; the firmware has
   `AMC_RESET_PROFILE = 0x1E`. Unused.

## Keychron Launcher exports

Checked against three real exports (now in `tests/fixtures/launcher/`):

- File name `HEProfile-<name>-<yyyy-mm-dd>.json`; there is no name inside (`profileNames` is empty), so the app
  shows `<name>`. `current` is the slot.
- `global` and every `keyMatrix[row][col]` entry hold `mode`, `distance`, `advance_press`, `advance_release`,
  `advanceMode` (`AKM_*`: 3 DKS, 4 gamepad, 5 toggle) and `dksIndex` (the advanced-mode data: DKS slot or
  joystick axis). For a key that follows the slot the top-level distances are filled in with the slot's values
  for display; the `edit` copy holds the raw values read from the keyboard (0 = follow). The app reads `edit`
  unless the entry is marked `dirty`.
- `snapTap`: SOCD pairs `{keys: [{row, col, ...}, {row, col, ...}], type, index}`.
- `analog` (gamepad key list), `curve` (global analog curve) and `dks` are not used; the gamepad axes are already
  in `advanceMode` / `dksIndex`.
- 11 of the 96 matrix positions have no key on the K2 HE; the Launcher leaves them at their defaults.

## Limits the UI enforces

From `profile.c`: actuation point 0.1 - 3.9 mm (`act_pt` 1 - 39), Rapid Trigger sensitivities 0.1 - 3.9 mm, all
in 0.1 mm steps; firmware defaults are 2.0 mm and 0.4 mm. Key travel is 4.0 mm (`FULL_TRAVEL_UNIT = 40`).

## Layouts and USB IDs

From `keyboard.json`: ANSI (`LAYOUT_ansi_84`) has USB PID `0x0E20`, ISO (`LAYOUT_iso_85`) `0x0E21`, JIS `0x0E22`.
`KeyboardDevice` opens exactly these, wired, on the raw-HID interface (usage page `0xFF60`, usage `0x61`), and
`DeviceWatcher` notices an unplug by enumerating, without sending packets. The dashboard draws the layout the
connected board reports.

## Not in the UI yet

Toggle-mode editing, dynamic keystrokes other than the "sprint" shape (they are kept and shown), and
calibration (`0x40` - `0x42`).
