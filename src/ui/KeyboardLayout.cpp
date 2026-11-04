#include "ui/KeyboardLayout.h"

#include <array>

namespace ui {

namespace {

    // Generated from Keychron/qmk_firmware (branch 2025q3, GPL-2.0-or-later):
    //   keyboards/keychron/k2_he/{iso,ansi}/keyboard.json  -> matrix position, x, y, w, h
    //   keyboards/keychron/k2_he/via_json/*.json           -> ISO Enter shape, keycap colour family
    // Legends are the Windows base layer of the default keymap (UK legends on ISO, US on ANSI).
    //
    //  row col       x     y      w    h      x2    y2   w2   h2   legend  shift  role  glyph
    // iso/keyboard.json (LAYOUT_iso_85), colours from via_json/k2_he_iso_rgb.json
    constexpr KeyGeometry K2_HE_ISO[] = {
        { 0,  0,    0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Esc", "", KeyRole::Accent, KeyGlyph::None},
        { 0,  1,    1.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F1", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  2,    2.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F2", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  3,    3.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F3", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  4,    4.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F4", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  5,    5.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F5", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  6,    6.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F6", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  7,    7.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F7", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  8,    8.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F8", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  9,    9.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F9", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 10,   10.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F10", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 11,   11.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F11", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 12,   12.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F12", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 13,   13.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Prt", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 14,   14.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Del", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 15,   15.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "RGB", "", KeyRole::Modifier, KeyGlyph::None},
        { 1,  0,    0.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "`", "¬", KeyRole::Modifier, KeyGlyph::None},
        { 1,  1,    1.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "1", "!", KeyRole::Alpha, KeyGlyph::None},
        { 1,  2,    2.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "2", "\"", KeyRole::Alpha, KeyGlyph::None},
        { 1,  3,    3.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "3", "£", KeyRole::Alpha, KeyGlyph::None},
        { 1,  4,    4.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "4", "$", KeyRole::Alpha, KeyGlyph::None},
        { 1,  5,    5.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "5", "%", KeyRole::Alpha, KeyGlyph::None},
        { 1,  6,    6.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "6", "^", KeyRole::Alpha, KeyGlyph::None},
        { 1,  7,    7.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "7", "&", KeyRole::Alpha, KeyGlyph::None},
        { 1,  8,    8.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "8", "*", KeyRole::Alpha, KeyGlyph::None},
        { 1,  9,    9.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "9", "(", KeyRole::Alpha, KeyGlyph::None},
        { 1, 10,   10.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "0", ")", KeyRole::Alpha, KeyGlyph::None},
        { 1, 11,   11.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "-", "_", KeyRole::Alpha, KeyGlyph::None},
        { 1, 12,   12.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "=", "+", KeyRole::Alpha, KeyGlyph::None},
        { 1, 13,   13.0f, 1.0f,   2.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Backspace", "", KeyRole::Modifier, KeyGlyph::None},
        { 1, 14,   15.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "PgUp", "", KeyRole::Modifier, KeyGlyph::None},
        { 2,  0,    0.0f, 2.0f,   1.5f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Tab", "", KeyRole::Modifier, KeyGlyph::None},
        { 2,  1,    1.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Q", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  2,    2.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "W", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  3,    3.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "E", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  4,    4.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "R", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  5,    5.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "T", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  6,    6.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Y", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  7,    7.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "U", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  8,    8.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "I", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  9,    9.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "O", "", KeyRole::Alpha, KeyGlyph::None},
        { 2, 10,   10.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "P", "", KeyRole::Alpha, KeyGlyph::None},
        { 2, 11,   11.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "[", "{", KeyRole::Alpha, KeyGlyph::None},
        { 2, 12,   12.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "]", "}", KeyRole::Alpha, KeyGlyph::None},
        { 2, 14,   15.0f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "PgDn", "", KeyRole::Modifier, KeyGlyph::None},
        { 3,  0,    0.0f, 3.0f,  1.75f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Caps", "", KeyRole::Modifier, KeyGlyph::None},
        { 3,  1,   1.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "A", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  2,   2.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "S", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  3,   3.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "D", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  4,   4.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  5,   5.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "G", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  6,   6.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "H", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  7,   7.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "J", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  8,   8.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "K", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  9,   9.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "L", "", KeyRole::Alpha, KeyGlyph::None},
        { 3, 10,  10.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ";", ":", KeyRole::Alpha, KeyGlyph::None},
        { 3, 11,  11.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "'", "@", KeyRole::Alpha, KeyGlyph::None},
        { 3, 12,  12.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "#", "~", KeyRole::Alpha, KeyGlyph::None},
        { 2, 13,  13.75f, 2.0f,  1.25f, 2.0f, -0.25f, 0.0f, 1.5f, 1.0f, "Enter", "", KeyRole::Accent, KeyGlyph::None},
        { 3, 13,   15.0f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Home", "", KeyRole::Modifier, KeyGlyph::None},
        { 4,  0,    0.0f, 4.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Shift", "", KeyRole::Modifier, KeyGlyph::None},
        { 4,  1,   1.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "\\", "|", KeyRole::Alpha, KeyGlyph::None},
        { 4,  2,   2.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Z", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  3,   3.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "X", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  4,   4.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "C", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  5,   5.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "V", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  6,   6.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "B", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  7,   7.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "N", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  8,   8.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "M", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  9,   9.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ",", "<", KeyRole::Alpha, KeyGlyph::None},
        { 4, 10,  10.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ".", ">", KeyRole::Alpha, KeyGlyph::None},
        { 4, 11,  11.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "/", "?", KeyRole::Alpha, KeyGlyph::None},
        { 4, 12,  12.25f, 4.0f,  1.75f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Shift", "", KeyRole::Modifier, KeyGlyph::None},
        { 4, 13,   14.0f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowUp},
        { 4, 14,   15.0f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "End", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  0,    0.0f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Ctrl", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  1,   1.25f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Win", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  2,    2.5f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Alt", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  6,   3.75f, 5.0f,  6.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Alpha, KeyGlyph::None},
        { 5,  9,   10.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "AltGr", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 10,   11.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Fn", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 11,   12.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Ctrl", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 12,   13.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowLeft},
        { 5, 13,   14.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowDown},
        { 5, 14,   15.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowRight},
    };

    // ansi/keyboard.json (LAYOUT_ansi_84), colours from via_json/k2_he_ansi_rgb.json
    constexpr KeyGeometry K2_HE_ANSI[] = {
        { 0,  0,    0.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Esc", "", KeyRole::Accent, KeyGlyph::None},
        { 0,  1,    1.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F1", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  2,    2.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F2", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  3,    3.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F3", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  4,    4.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F4", "", KeyRole::Alpha, KeyGlyph::None},
        { 0,  5,    5.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F5", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  6,    6.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F6", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  7,    7.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F7", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  8,    8.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F8", "", KeyRole::Modifier, KeyGlyph::None},
        { 0,  9,    9.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F9", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 10,   10.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F10", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 11,   11.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F11", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 12,   12.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F12", "", KeyRole::Alpha, KeyGlyph::None},
        { 0, 13,   13.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Prt", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 14,   14.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Del", "", KeyRole::Modifier, KeyGlyph::None},
        { 0, 15,   15.0f, 0.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "RGB", "", KeyRole::Modifier, KeyGlyph::None},
        { 1,  0,    0.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "`", "~", KeyRole::Modifier, KeyGlyph::None},
        { 1,  1,    1.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "1", "!", KeyRole::Alpha, KeyGlyph::None},
        { 1,  2,    2.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "2", "@", KeyRole::Alpha, KeyGlyph::None},
        { 1,  3,    3.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "3", "#", KeyRole::Alpha, KeyGlyph::None},
        { 1,  4,    4.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "4", "$", KeyRole::Alpha, KeyGlyph::None},
        { 1,  5,    5.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "5", "%", KeyRole::Alpha, KeyGlyph::None},
        { 1,  6,    6.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "6", "^", KeyRole::Alpha, KeyGlyph::None},
        { 1,  7,    7.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "7", "&", KeyRole::Alpha, KeyGlyph::None},
        { 1,  8,    8.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "8", "*", KeyRole::Alpha, KeyGlyph::None},
        { 1,  9,    9.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "9", "(", KeyRole::Alpha, KeyGlyph::None},
        { 1, 10,   10.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "0", ")", KeyRole::Alpha, KeyGlyph::None},
        { 1, 11,   11.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "-", "_", KeyRole::Alpha, KeyGlyph::None},
        { 1, 12,   12.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "=", "+", KeyRole::Alpha, KeyGlyph::None},
        { 1, 13,   13.0f, 1.0f,   2.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Backspace", "", KeyRole::Modifier, KeyGlyph::None},
        { 1, 14,   15.0f, 1.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "PgUp", "", KeyRole::Modifier, KeyGlyph::None},
        { 2,  0,    0.0f, 2.0f,   1.5f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Tab", "", KeyRole::Modifier, KeyGlyph::None},
        { 2,  1,    1.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Q", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  2,    2.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "W", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  3,    3.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "E", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  4,    4.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "R", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  5,    5.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "T", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  6,    6.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Y", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  7,    7.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "U", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  8,    8.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "I", "", KeyRole::Alpha, KeyGlyph::None},
        { 2,  9,    9.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "O", "", KeyRole::Alpha, KeyGlyph::None},
        { 2, 10,   10.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "P", "", KeyRole::Alpha, KeyGlyph::None},
        { 2, 11,   11.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "[", "{", KeyRole::Alpha, KeyGlyph::None},
        { 2, 12,   12.5f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "]", "}", KeyRole::Alpha, KeyGlyph::None},
        { 2, 13,   13.5f, 2.0f,   1.5f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "\\", "|", KeyRole::Modifier, KeyGlyph::None},
        { 2, 14,   15.0f, 2.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "PgDn", "", KeyRole::Modifier, KeyGlyph::None},
        { 3,  0,    0.0f, 3.0f,  1.75f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Caps", "", KeyRole::Modifier, KeyGlyph::None},
        { 3,  1,   1.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "A", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  2,   2.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "S", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  3,   3.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "D", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  4,   4.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "F", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  5,   5.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "G", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  6,   6.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "H", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  7,   7.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "J", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  8,   8.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "K", "", KeyRole::Alpha, KeyGlyph::None},
        { 3,  9,   9.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "L", "", KeyRole::Alpha, KeyGlyph::None},
        { 3, 10,  10.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ";", ":", KeyRole::Alpha, KeyGlyph::None},
        { 3, 11,  11.75f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "'", "\"", KeyRole::Alpha, KeyGlyph::None},
        { 3, 12,  12.75f, 3.0f,  2.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Enter", "", KeyRole::Accent, KeyGlyph::None},
        { 3, 13,   15.0f, 3.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Home", "", KeyRole::Modifier, KeyGlyph::None},
        { 4,  0,    0.0f, 4.0f,  2.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Shift", "", KeyRole::Modifier, KeyGlyph::None},
        { 4,  2,   2.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Z", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  3,   3.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "X", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  4,   4.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "C", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  5,   5.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "V", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  6,   6.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "B", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  7,   7.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "N", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  8,   8.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "M", "", KeyRole::Alpha, KeyGlyph::None},
        { 4,  9,   9.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ",", "<", KeyRole::Alpha, KeyGlyph::None},
        { 4, 10,  10.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, ".", ">", KeyRole::Alpha, KeyGlyph::None},
        { 4, 11,  11.25f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "/", "?", KeyRole::Alpha, KeyGlyph::None},
        { 4, 12,  12.25f, 4.0f,  1.75f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Shift", "", KeyRole::Modifier, KeyGlyph::None},
        { 4, 13,   14.0f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowUp},
        { 4, 14,   15.0f, 4.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "End", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  0,    0.0f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Ctrl", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  1,   1.25f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Win", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  2,    2.5f, 5.0f,  1.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Alt", "", KeyRole::Modifier, KeyGlyph::None},
        { 5,  6,   3.75f, 5.0f,  6.25f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Alpha, KeyGlyph::None},
        { 5,  9,   10.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Alt", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 10,   11.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Fn", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 11,   12.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "Ctrl", "", KeyRole::Modifier, KeyGlyph::None},
        { 5, 12,   13.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowLeft},
        { 5, 13,   14.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowDown},
        { 5, 14,   15.0f, 5.0f,   1.0f, 1.0f,   0.0f, 0.0f, 0.0f, 0.0f, "", "", KeyRole::Modifier, KeyGlyph::ArrowRight},
    };

    constexpr std::array<LayoutKind, 2> LAYOUT_KINDS{LayoutKind::Iso, LayoutKind::Ansi};

    constexpr float layoutWidth(std::span<const KeyGeometry> keys) {
        float width = 0.0f;
        for (const KeyGeometry& key : keys) width = width > key.x + key.w ? width : key.x + key.w;
        return width;
    }

    constexpr float layoutHeight(std::span<const KeyGeometry> keys) {
        float height = 0.0f;
        for (const KeyGeometry& key : keys) height = height > key.y + key.h ? height : key.y + key.h;
        return height;
    }

    const KeyboardLayout ISO_LAYOUT{LayoutKind::Iso, "ISO (UK)", layoutWidth(K2_HE_ISO), layoutHeight(K2_HE_ISO), K2_HE_ISO};
    const KeyboardLayout ANSI_LAYOUT{LayoutKind::Ansi, "ANSI (US)", layoutWidth(K2_HE_ANSI), layoutHeight(K2_HE_ANSI), K2_HE_ANSI};

} // namespace

const KeyGeometry* KeyboardLayout::find(const KeyPosition& position) const noexcept {
    for (const KeyGeometry& key : keys) {
        if (key.row == position.row && key.col == position.col) return &key;
    }
    return nullptr;
}

const KeyboardLayout& keyboardLayout(const LayoutKind kind) noexcept {
    return kind == LayoutKind::Ansi ? ANSI_LAYOUT : ISO_LAYOUT;
}

std::span<const LayoutKind> availableLayouts() noexcept {
    return LAYOUT_KINDS;
}

bool isKeyOnBoard(const KeyPosition& position) noexcept {
    for (const LayoutKind kind : LAYOUT_KINDS) {
        if (keyboardLayout(kind).find(position) != nullptr) return true;
    }
    return false;
}

} // namespace ui
