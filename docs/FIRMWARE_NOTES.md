# Firmware notes

The K2 HE runs Keychron's open-source QMK firmware (<https://github.com/Keychron/qmk_firmware>). This
document compares what `KeychronProtocol` / `ProfileService` assume with what the firmware source does.

Sources read: branch `2025q3` at `2b493ff` (2026-10-23) and `hall_effect_playground` at `bc56b3c`
(2025-12-01); the files are `keyboards/keychron/common/keychron_raw_hid.c`,
`keyboards/keychron/common/analog_matrix/{analog_matrix.c,profile.c,analog_matrix_type.h,analog_matrix_eeconfig.h}`
and `keyboards/keychron/k2_he/info.json`. Both branches have the same command set.
Keychron's website and the Launcher web app could not be reached from the sandbox the work was done in, so
the Launcher's own UI was not inspected; the Launcher is a front end for the same commands.

**None of this was tested on a keyboard.** Treat the "differs" rows as things to verify, not as proven bugs:
the shipping firmware could differ from the public source.

## What matches

| Item | Firmware | `KeychronProtocol` |
|---|---|---|
| Analog-matrix command | `KC_ANALOG_MATRIX = 0xA9` | `CMD_ANALOG_MATRIX` |
| `0x10` info | replies `[2]` = active profile, `[3]` = profile count | slot read from `response[2]` |
| `0x11` select | `profile_select(data[2])` | `createSlotSelectPacket` |
| `0x1F` save | `profile_save(data[2])` commits the RAM profile to EEPROM | `createSaveProfilePacket` |
| `0x30` live travel | `AMC_GET_REALTIME_TRAVEL` | `createRealtimeTravelPollPacket` |
| Profile size | `sizeof(analog_matrix_profile_t)` = 860 bytes for the K2 HE's 6 x 16 matrix | `PROFILE_MEMORY_SIZE = 860` |
| Chunk size | a raw report has 26 data bytes (`data[6..31]`) | `MAX_CHUNK_PAYLOAD = 26` |
| Status byte | every setter answers `data[2] = success ? 0 : 1` | the UI now checks it |

The `memory` array in the profile files is therefore a raw dump of `analog_matrix_profile_t`. Its first four
bytes are the global config (packed bitfields: mode 2 bits, actuation 6, press sensitivity 6, release
sensitivity 6, advanced mode 4, advanced data 8). `kbd::ProfileMemory` decodes it; the layout was checked by
compiling the firmware's own struct and comparing bytes (`tests/test_profile_memory.cpp`).

## What differs (verify on hardware)

1. **`0x12` is a read, not a write.** `AMC_GET_PROFILE_RAW` copies the stored profile bytes *into* the reply
   (`profile_get_raw_data`); neither branch has a "write raw profile" command. `generateMemoryStreamPackets`
   / `ProfileService::flashProfileMemory` send 34 packets with this opcode as if writing, then `0x1F`
   saves the RAM profile, which would be unchanged. So *Write to keyboard* may not change what the keyboard
   holds. Loading a profile in the Launcher most likely replays the setters (`0x13` name, `0x14` travel with
   per-key masks, `0x15` advanced mode, `0x16` SOCD) before `0x1F`.
   *The UI reads the settings back after a write and shows a warning when they differ from the file.*

2. **Rapid Trigger mode byte.** `AMC_SET_TRAVEL` (`0x14`) byte 3 is the firmware's `AKM_*` mode:
   `1 = regular`, `2 = rapid trigger`; `profile_set_traval` rejects anything above 2 and rejects `0`
   (`AKM_GLOBAL`) when "entire keyboard" is set. `createFastTravelPacket` sends `rapidTrigger ? 1 : 0`, and
   `FastTravelPacketStructure` asserts that. Read literally: "Rapid Trigger on" selects regular mode and
   "off" is rejected. Actuation (byte 4) and sensitivities (bytes 5, 6) match.
   *The UI checks the acknowledgement and reads the stored mode back, and says so if the Rapid Trigger flag
   does not match.*

3. **Global values only take effect on the next profile switch.** For the "entire keyboard" case
   `profile_set_traval` updates the RAM profile but does not call `update_travel_configs()`; the tables the
   key scanner uses are rebuilt in `profile_select()` (to a *different* slot), on boot, after calibration and
   after `AMC_RESET_PROFILE`. *When you edit the active slot the UI hops to a neighbouring slot and back.*

4. **Profile name** (`0x13`): firmware reads `[2]` = slot, `[3]` = length (at most 28), `[4..]` = text.
   `createSetProfileNamePacket` has no length byte and starts the text at byte 3, so the first character is
   read as the length (for example `'T'` = 84, rejected). Not used by the UI.

5. **SOCD / Snap Tap**: the firmware command is `AMC_SET_SOCD = 0x16` under `0xA9`
   (`[2]` profile, `[3..6]` the two key positions, `[7]` index, `[8]` type). `createSocdPairPacket` uses
   `0xA8` / `0x09`, which `keychron_raw_hid.c` does not handle. Not used by the UI.

6. **Opcode naming**: `AMC_CLEAR_PROFILE = 0x1D`; the firmware's is `AMC_RESET_PROFILE = 0x1E`. Not used.

## Limits the UI enforces

From `profile.c`: actuation point 0.1 - 3.9 mm (`act_pt` 1 - 39, 0 means "default", above 40 is reset on
load), Rapid Trigger sensitivities 0.1 - 3.9 mm, all in 0.1 mm steps; firmware defaults are 2.0 mm and
0.4 mm. Key travel is 4.0 mm (`FULL_TRAVEL_UNIT = 40`).

## Features the firmware offers that the UI does not (yet)

Per-key actuation, dynamic keystrokes (`ADV_MODE_OKMC`, 20 slots), toggle mode, game-controller mode and
curve, SOCD pairs (20), live key travel (`0x30`) and calibration (`0x40` - `0x42`). These are the Launcher's
other HE pages; the per-key settings need the per-key bytes of the profile dump that `ProfileMemory`
already documents.
