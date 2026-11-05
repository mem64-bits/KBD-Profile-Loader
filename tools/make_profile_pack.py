#!/usr/bin/env python3
"""Generates the ready-made profile pack in profiles/ (Keychron Launcher JSON, K2 HE ISO).

    python3 tools/make_profile_pack.py [output-dir]

Every file has the shape of the Launcher exports in tests/fixtures/launcher/ ("global", "keyMatrix",
"snapTap", "analog", ...), so both this app and the Keychron Launcher can import it. The file name carries
the profile name: HEProfile-<name>-<date>.json. docs/PROFILE_PACK.md explains each profile.

Units: travel values are 0.1 mm (20 = 2.0 mm). Per-key mode 0 = follow the slot, 1 = regular, 2 = Rapid
Trigger. A per-key distance of 0 also means "follow the slot", so never write 0 for an override.
Gamepad keys: advanceMode 4 and dksIndex = target (GamepadTarget in headers/kbd/GameController.h).
"""
import json
import sys
from pathlib import Path

DATE = "2026-11-04"
DEVICE_ID = 875826721  # K2 HE ISO, same as the Launcher exports in tests/fixtures

# ---- matrix: legend -> (row, col), from src/ui/KeyboardLayout.cpp (ISO 85) ---------------------------------
KEYS = {}
for c, n in enumerate(["Esc", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "Prt", "Del", "RGB"]):
    KEYS[n] = (0, c)
for c, n in enumerate(["`", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Bksp", "PgUp"]):
    KEYS[n] = (1, c)
for c, n in enumerate(["Tab", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "PgDn"]):
    KEYS[n] = (2, c)
for c, n in enumerate(["Caps", "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "#", "Home"]):
    KEYS[n] = (3, c)  # the ISO Enter is matrix (2, 13), so row 3 has no key between # and Home
for c, n in enumerate(["LShift", "\\", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "RShift", "Up", "End"]):
    KEYS[n] = (4, c)
KEYS.update({"LCtrl": (5, 0), "Win": (5, 1), "LAlt": (5, 2), "Space": (5, 6), "AltGr": (5, 9), "Fn": (5, 10),
             "RCtrl": (5, 11), "Left": (5, 12), "Down": (5, 13), "Right": (5, 14)})

# legend -> (HID usage, QMK code) for the "analog" block, which the Launcher uses to draw gamepad keys
HID = {"Esc": (41, "KC_ESC"), "Bksp": (42, "KC_BSPC"), "Tab": (43, "KC_TAB"), "Space": (44, "KC_SPC"),
       "Enter": (40, "KC_ENT"), "Caps": (57, "KC_CAPS"), "Home": (74, "KC_HOME"), "PgUp": (75, "KC_PGUP"),
       "PgDn": (78, "KC_PGDN"), "End": (77, "KC_END"), "Del": (76, "KC_DEL"), "Prt": (70, "KC_PSCR"),
       "Up": (82, "KC_UP"), "Down": (81, "KC_DOWN"), "Left": (80, "KC_LEFT"), "Right": (79, "KC_RGHT"),
       "LCtrl": (224, "KC_LCTL"), "LShift": (225, "KC_LSFT"), "LAlt": (226, "KC_LALT"), "Win": (227, "KC_LGUI"),
       "RCtrl": (228, "KC_RCTL"), "RShift": (229, "KC_RSFT"), "`": (53, "KC_GRV"), "-": (45, "KC_MINS"),
       "=": (46, "KC_EQL"), "[": (47, "KC_LBRC"), "]": (48, "KC_RBRC"), ";": (51, "KC_SCLN"), "'": (52, "KC_QUOT"),
       ",": (54, "KC_COMM"), ".": (55, "KC_DOT"), "/": (56, "KC_SLSH"), "#": (50, "KC_NUHS"), "\\": (100, "KC_NUBS")}
for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    HID[ch] = (4 + i, "KC_" + ch)
for i, ch in enumerate("1234567890"):
    HID[ch] = (30 + i, "KC_" + ch)
for i in range(12):
    HID[f"F{i + 1}"] = (58 + i, f"KC_F{i + 1}")

# ---- gamepad targets (GamepadTarget in headers/kbd/GameController.h) --------------------------------------
LS_L, LS_R, LS_D, LS_U, LT, RT, RS_L, RS_R, RS_D, RS_U = range(10)
A, B, X, Y, LB, RB, BACK, START, L3, R3, DP_U, DP_D, DP_L, DP_R, GUIDE = (13 + i for i in range(15))

CURVES = {  # 4 points (x = 0.1 mm of travel, y = stick deflection 0-127), x never decreases
    "standard":   [(2, 0), (12, 40), (32, 127), (40, 127)],   # linear, full deflection at 3.2 mm
    "precise":    [(2, 0), (16, 20), (32, 70), (38, 127)],    # slow start (the app's "Precise" preset)
    "responsive": [(2, 0), (8, 60), (20, 110), (32, 127)],    # strong early response ("Responsive" preset)
    "steer":      [(2, 0), (14, 22), (28, 70), (36, 127)],    # fine near the centre, full at 3.6 mm
}

# ---- gamepad layouts --------------------------------------------------------------------------------------
LEFT_STICK = {"W": LS_U, "A": LS_L, "S": LS_D, "D": LS_R}
ARROWS_RS = {"Up": RS_U, "Left": RS_L, "Down": RS_D, "Right": RS_R}
ARROWS_DPAD = {"Up": DP_U, "Left": DP_L, "Down": DP_D, "Right": DP_R}
IJKL_FACES = {"I": Y, "J": X, "K": A, "L": B}            # the diamond under the right hand: I top, K bottom
IJKL_RS = {"I": RS_U, "J": RS_L, "K": RS_D, "L": RS_R}   # analog channels in the same diamond
TFGH_DPAD = {"T": DP_U, "F": DP_L, "G": DP_D, "H": DP_R}

FULL = {**LEFT_STICK, **ARROWS_RS, **IJKL_FACES, **TFGH_DPAD, "Q": LB, "E": RB, "LAlt": LT, "Space": RT,
        "LShift": L3, "RShift": R3, "Tab": BACK, "Enter": START, "Home": GUIDE}

RACING_BASE = {"A": LS_L, "D": LS_R, "W": RT, "S": LT, "Q": LB, "E": RB, "Enter": START, "Tab": BACK,
               "Z": A, "X": B, "C": X, "V": Y}


def merge(*parts):
    out = {}
    for p in parts:
        out.update(p)
    return out


# ---- the profiles -----------------------------------------------------------------------------------------
# Each: name, global (mode, act, press, release), keys {legend: (mode, act, press, release)} for typed keys,
# pad {legend: target}, curve, group, summary
RT_ = 2
REG = 1

def rt(act, press, rel=None):
    return (RT_, act, press, press if rel is None else rel)

def reg(act):
    return (REG, act, 0, 0)

def _lol(abilities, summoners, items, extra):
    keys = {}
    for k in "QWER":
        keys[k] = abilities
    for k in "DF":
        keys[k] = summoners
    for k in "1234567":
        keys[k] = items
    keys.update(extra)
    return keys

PROFILES = [
    # ---------------------------------------------------------------- League of Legends (typed, no gamepad)
    dict(name="LoL Ranked", group="PC", glob=(REG, 18, 4, 4), curve="standard",
         summary="Quick-cast play: Rapid Trigger on Q W E R and A / S, deeper D F, protected recall.",
         keys=_lol(rt(12, 3), reg(18), reg(16),
                   {"A": rt(12, 2), "S": rt(12, 2), "B": reg(30), "Y": reg(25), "Space": reg(15), "Tab": reg(15),
                    "LShift": reg(12), "LCtrl": reg(12), "4": reg(20), "Esc": reg(20), "Enter": reg(15)})),
    dict(name="LoL Casual", group="PC", glob=(REG, 20, 4, 4), curve="standard",
         summary="Relaxed travel for Normals, ARAM and chat-heavy games: Rapid Trigger on abilities only.",
         keys=_lol(rt(15, 4), reg(25), reg(20), {"B": reg(35), "Y": reg(30), "Space": reg(18), "4": reg(25)})),
    dict(name="LoL ADC Kiting", group="PC", glob=(REG, 16, 3, 3), curve="standard",
         summary="Marksmen: very short reset on A (attack move), S (stop) and abilities for stutter-stepping.",
         keys=_lol(rt(10, 2), reg(16), reg(14),
                   {"A": rt(8, 2), "S": rt(8, 2), "B": reg(30), "Y": reg(25), "Space": reg(15), "Tab": reg(15),
                    "LShift": reg(10), "LCtrl": reg(12), "4": reg(20), "Esc": reg(20), "Enter": reg(15)})),
    # ---------------------------------------------------------------- PC analog movement
    dict(name="PC Analog WASD", group="PC", glob=(REG, 20, 4, 4), curve="precise",
         summary="WASD becomes a real left stick (walk by pressing lightly, run by pressing deep). Nothing else changes.",
         pad=dict(LEFT_STICK)),
    dict(name="PC Analog WASD Actions", group="PC", glob=(REG, 20, 4, 4), curve="precise",
         summary="Analog WASD plus a full pad on the keys around it, for PC games that take a controller.",
         pad=merge(LEFT_STICK, ARROWS_RS, {"Space": A, "LCtrl": B, "E": X, "R": Y, "Q": LB, "F": RB, "LShift": LT,
                                          "LAlt": RT, "C": L3, "V": R3, "Tab": BACK, "Enter": START,
                                          "1": DP_U, "2": DP_R, "3": DP_D, "4": DP_L})),
    # ---------------------------------------------------------------- Emulation
    dict(name="EMU Full Controller", group="EMU", glob=(REG, 20, 4, 4), curve="standard",
         summary="Every XInput input on the keyboard: two analog sticks, analog triggers, D-pad, face, bumpers, clicks.",
         pad=dict(FULL)),
    dict(name="PSX Dual Analog", group="EMU", glob=(REG, 20, 4, 4), curve="responsive",
         summary="DualShock-era PlayStation (DuckStation): two sticks, digital L1 R1 L2 R2, no pressure.",
         pad={k: v for k, v in FULL.items() if k != "Home"}),
    dict(name="PSX Racing NeGcon", group="EMU", glob=(REG, 20, 4, 4), curve="steer",
         summary="Ridge Racer / Wipeout style: A and D are the twist, W is II (gas, analog), S is I (brake, analog).",
         pad=merge(RACING_BASE, ARROWS_DPAD)),
    dict(name="PS2 DualShock2 Pressure", group="EMU", glob=(REG, 20, 4, 4), curve="standard",
         summary="PCSX2 dual-analog pad; Caps Lock is the Pressure Modifier button (light press on any button).",
         pad=merge(FULL, {"Caps": GUIDE}, {"Home": None})),
    dict(name="PS2 Pressure Racing", group="EMU", glob=(REG, 20, 4, 4), curve="steer",
         summary="Gran Turismo / Burnout: W gas and S brake are analog; I J K L are four extra analog channels.",
         pad=merge(RACING_BASE, ARROWS_DPAD, IJKL_RS)),
    dict(name="PS2 Pressure Faces", group="EMU", glob=(REG, 20, 4, 4), curve="standard",
         summary="Metal Gear / Star Ocean / Mad Maestro: Cross Circle Square Triangle on I J K L, each with real depth.",
         pad=merge(LEFT_STICK, IJKL_RS, ARROWS_DPAD, {"Q": LB, "E": RB, "LAlt": LT, "Space": RT, "LShift": L3,
                                                       "RShift": R3, "Tab": BACK, "Enter": START,
                                                       "Caps": GUIDE})),
    dict(name="PS3 DualShock3 RPCS3", group="EMU", glob=(REG, 20, 4, 4), curve="standard",
         summary="RPCS3 dual-analog pad with analog L2 / R2; Caps Lock is the Pressure Intensity button.",
         pad=merge(FULL, {"Caps": GUIDE}, {"Home": None})),
    dict(name="PS3 Xbox360 Racing", group="EMU", glob=(REG, 20, 4, 4), curve="steer",
         summary="Gran Turismo 5/6, DiRT, Forza (Xenia): analog gas and brake, camera on the arrows.",
         pad=merge({k: v for k, v in RACING_BASE.items() if k not in "ZXCV"}, ARROWS_RS, IJKL_FACES, TFGH_DPAD,
                   {"LShift": L3, "RShift": R3})),
    dict(name="GameCube Wii Dolphin", group="EMU", glob=(REG, 20, 4, 4), curve="precise",
         summary="Dolphin: stick plus C-stick on the arrows, Z on E, analog L / R for light and hard shield or brake.",
         pad=merge(LEFT_STICK, ARROWS_RS, IJKL_FACES, TFGH_DPAD, {"E": RB, "LAlt": LT, "Space": RT, "Enter": START,
                                                                   "Q": LB})),
    dict(name="N64 Single Stick", group="EMU", glob=(REG, 20, 4, 4), curve="precise",
         summary="N64: one stick with walking-speed control, C buttons on the arrows, Z on Left Alt.",
         pad=merge(LEFT_STICK, ARROWS_RS, {"K": A, "J": B, "LAlt": LT, "Q": LB, "E": RB, "Enter": START},
                   TFGH_DPAD)),
]


# ---- builders ---------------------------------------------------------------------------------------------
def travel(mode, act, press, rel):
    return {"mode": mode, "distance": act, "advance_press": press, "advance_release": rel}


def key_entry(row, col, glob, override=None, target=None):
    g = glob
    if target is not None:
        top, edit = travel(0, g[1], g[2], g[3]), travel(0, 0, 0, 0)
        adv, idx = 4, target
    elif override is not None:
        m, act, press, rel = override
        if m == REG:  # regular: sensitivity fields are unused; keep the slot's values for display
            press, rel = g[2], g[3]
            edit = travel(1, act, 0, 0)
        else:
            edit = travel(m, act, press, rel)
        top = travel(m, act, press, rel)
        adv, idx = 0, None
    else:
        top, edit = travel(0, g[1], g[2], g[3]), travel(0, 0, 0, 0)
        adv, idx = 0, None
    entry = dict(top)
    entry.update({"dksIndex": idx, "advanceMode": adv, "row": row, "col": col, "dirty": False, "edit": edit})
    return entry


def key_info(name):
    row, col = KEYS[name]
    val, code = HID[name]
    return {"val": val, "code": code, "hex": format(val, "x"), "name": name, "longName": name, "col": col,
            "row": row, "total": 85}


def build(profile):
    g = profile["glob"]
    pad = {k: v for k, v in profile.get("pad", {}).items() if v is not None}
    keys = profile.get("keys", {})
    for name in list(pad) + list(keys):
        assert name in KEYS, f"unknown key {name!r} in {profile['name']}"
    assert not (set(pad) & set(keys)), f"key both typed and pad in {profile['name']}"
    targets = list(pad.values())
    assert len(targets) == len(set(targets)), f"two keys drive one target in {profile['name']}"
    assert all(t != 12 and 0 <= t < 45 for t in targets)

    by_pos_pad = {KEYS[n]: t for n, t in pad.items()}
    by_pos_key = {KEYS[n]: v for n, v in keys.items()}
    matrix = []
    for r in range(6):
        row = []
        for c in range(16):
            row.append(key_entry(r, c, g, by_pos_key.get((r, c)), by_pos_pad.get((r, c))))
        matrix.append(row)

    base = Path(__file__).resolve().parent.parent / "tests" / "fixtures" / "launcher" / "HEProfile-Standard-2026-10-28.json"
    template = json.loads(base.read_text())
    curve = [list(p) for p in CURVES[profile["curve"]]]
    out = {
        "global": {**travel(g[0], g[1], g[2], g[3]), "dksIndex": None, "advanceMode": 0, "dirty": False,
                   "edit": travel(g[0], g[1], g[2], g[3])},
        "keyMatrix": matrix,
        "snapTap": [],
        "dks": template["dks"],
        "analog": {str(t): key_info(n) for n, t in sorted(pad.items(), key=lambda kv: kv[1])},
        "toggle": [], "dkr": [], "turbo": [],
        "profileNames": ["", "", ""],
        "size": 860, "current": 0, "total": 3,
        "curve": curve,
        "deviceId": DEVICE_ID,
    }
    return out


def main():
    out_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "profiles"
    out_dir.mkdir(parents=True, exist_ok=True)
    for p in PROFILES:
        path = out_dir / f"HEProfile-{p['name']}-{DATE}.json"
        path.write_text(json.dumps(build(p), separators=(",", ":"), ensure_ascii=False), encoding="utf-8")
        print(path.name)


if __name__ == "__main__":
    main()
