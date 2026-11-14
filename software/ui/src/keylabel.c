#include "keylabel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"   /* LV_SYMBOL_* だけを使う */

/* タブを開く USER キー（keymap.rs と同じ） */
static const char *const user_names[] = { "Keys", "Pad", "Media", "System" };

/* 数字と記号の段：Shift を押したときの記号（US 配列）を上に小さく添える */
static const char *shifted(unsigned hid)
{
    static const char *const digits[] = { "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" };
    if (hid >= 0x1E && hid <= 0x27)
        return digits[hid - 0x1E];
    switch (hid) {
    case 0x2D: return "_";
    case 0x2E: return "+";
    case 0x2F: return "{";
    case 0x30: return "}";
    case 0x31: return "|";
    case 0x33: return ":";
    case 0x34: return "\"";
    case 0x35: return "~";
    case 0x36: return "<";
    case 0x37: return ">";
    case 0x38: return "?";
    default: return NULL;
    }
}

static const char *hid_name(unsigned hid, char *tmp, size_t n)
{
    if (hid >= 0x04 && hid <= 0x1D) {
        snprintf(tmp, n, "%c", 'A' + (int)(hid - 0x04));
        return tmp;
    }
    if (hid >= 0x1E && hid <= 0x26) {
        snprintf(tmp, n, "%c", '1' + (int)(hid - 0x1E));
        return tmp;
    }
    if (hid >= 0x3A && hid <= 0x45) {
        snprintf(tmp, n, "F%u", hid - 0x3A + 1);
        return tmp;
    }
    if (hid >= 0x68 && hid <= 0x73) {
        snprintf(tmp, n, "F%u", hid - 0x68 + 13);
        return tmp;
    }
    if (hid >= 0x59 && hid <= 0x61) {
        snprintf(tmp, n, "KP%u", hid - 0x59 + 1);
        return tmp;
    }
    switch (hid) {
    case 0x27: return "0";
    case 0x28: return "Enter";
    case 0x29: return "Esc";
    case 0x2A: return LV_SYMBOL_BACKSPACE;
    case 0x2B: return "Tab";
    case 0x2C: return "Space";
    case 0x2D: return "-";
    case 0x2E: return "=";
    case 0x2F: return "[";
    case 0x30: return "]";
    case 0x31: return "\\";
    case 0x32: return "#";
    case 0x33: return ";";
    case 0x34: return "'";
    case 0x35: return "`";
    case 0x36: return ",";
    case 0x37: return ".";
    case 0x38: return "/";
    case 0x39: return "Caps";
    case 0x46: return "PrtSc";
    case 0x47: return "ScrLk";
    case 0x48: return "Pause";
    case 0x49: return "Ins";
    case 0x4A: return "Home";
    case 0x4B: return "PgUp";
    case 0x4C: return "Del";
    case 0x4D: return "End";
    case 0x4E: return "PgDn";
    case 0x4F: return LV_SYMBOL_RIGHT;
    case 0x50: return LV_SYMBOL_LEFT;
    case 0x51: return LV_SYMBOL_DOWN;
    case 0x52: return LV_SYMBOL_UP;
    case 0x53: return "NumLk";
    case 0x54: return "KP/";
    case 0x55: return "KP*";
    case 0x56: return "KP-";
    case 0x57: return "KP+";
    case 0x58: return "KPEnt";
    case 0x62: return "KP0";
    case 0x63: return "KP.";
    case 0x65: return "Menu";
    /* RMK の HidKeyCode が 0xA5〜0xBE に置いているシステム・メディアのキー */
    case 0xA5: return "Power";
    case 0xA6: return "Sleep";
    case 0xA7: return "Wake";
    case 0xA8: return LV_SYMBOL_MUTE;
    case 0xA9: return LV_SYMBOL_VOLUME_MAX;
    case 0xAA: return LV_SYMBOL_VOLUME_MID;
    case 0xAB: return LV_SYMBOL_NEXT;
    case 0xAC: return LV_SYMBOL_PREV;
    case 0xAD: return LV_SYMBOL_STOP;
    case 0xAE: return LV_SYMBOL_PLAY;
    case 0xAF: return "Media";
    case 0xB0: return LV_SYMBOL_EJECT;
    case 0xBD: return "Bri+";
    case 0xBE: return "Bri-";
    case 0xE0: return "Ctrl";
    case 0xE1: return "Shift";
    case 0xE2: return "Alt";
    case 0xE3: return "GUI";
    case 0xE4: return "RCtrl";
    case 0xE5: return "RShift";
    case 0xE6: return "RAlt";
    case 0xE7: return "RGUI";
    default:
        snprintf(tmp, n, "0x%02X", hid);
        return tmp;
    }
}

static const char *consumer_name(unsigned usage, char *tmp, size_t n)
{
    switch (usage) {
    case 0xE2: return LV_SYMBOL_MUTE;
    case 0xE9: return LV_SYMBOL_VOLUME_MAX;
    case 0xEA: return LV_SYMBOL_VOLUME_MID;
    case 0xB5: return LV_SYMBOL_NEXT;
    case 0xB6: return LV_SYMBOL_PREV;
    case 0xB7: return LV_SYMBOL_STOP;
    case 0xCD: return LV_SYMBOL_PLAY;
    case 0x6F: return "Bri+";
    case 0x70: return "Bri-";
    default:
        snprintf(tmp, n, "C%X", usage);
        return tmp;
    }
}

/* 修飾キーのビットを "Ctrl+Shift" のようにする */
static void mods_name(unsigned mods, char *out, size_t n)
{
    static const char *const names[] = { "Ctrl", "Shift", "Alt", "GUI", "RCtrl", "RShift", "RAlt", "RGUI" };
    out[0] = '\0';
    for (int b = 0; b < 8; b++) {
        if (!(mods & (1u << b)))
            continue;
        size_t len = strlen(out);
        snprintf(out + len, n - len, "%s%s", len ? "+" : "", names[b]);
    }
}

/* 1 つの動作（'/' を含まない）を表示にする */
static void format_one(const char *code, char *out, size_t n)
{
    char tmp[16], mods[48];
    unsigned v = 0, m = 0;
    const char *arg = code + 1;

    switch (code[0]) {
    case '\0':
    case '-':
        out[0] = '\0';
        return;
    case 'K':
        if (sscanf(arg, "%x+%x", &v, &m) == 2) {
            mods_name(m, mods, sizeof mods);
            /* Shift だけが付いた記号は、その記号で見せる */
            if (m == 0x02 && shifted(v)) {
                snprintf(out, n, "%s", shifted(v));
                return;
            }
            snprintf(out, n, "%s+%s", mods, hid_name(v, tmp, sizeof tmp));
            return;
        }
        if (sscanf(arg, "%x", &v) != 1)
            break;
        if (shifted(v))
            snprintf(out, n, "%s\n%s", shifted(v), hid_name(v, tmp, sizeof tmp));
        else
            snprintf(out, n, "%s", hid_name(v, tmp, sizeof tmp));
        return;
    case 'C':
        if (sscanf(arg, "%x", &v) != 1)
            break;
        snprintf(out, n, "%s", consumer_name(v, tmp, sizeof tmp));
        return;
    case 'S':
        snprintf(out, n, "Sys %s", arg);
        return;
    case 'M':
        if (sscanf(arg, "%x", &v) != 1)
            break;
        mods_name(v, mods, sizeof mods);
        snprintf(out, n, "%s", mods);
        return;
    case 'L':
        snprintf(out, n, "L%s", arg);
        return;
    case 'G':
        snprintf(out, n, "TG%s", arg);
        return;
    case 'D':
        snprintf(out, n, "DF%s", arg);
        return;
    case 'O':
        snprintf(out, n, "OSL%s", arg);
        return;
    case 'U':
        v = (unsigned)atoi(arg);
        if (v < sizeof user_names / sizeof user_names[0])
            snprintf(out, n, "%s", user_names[v]);
        else
            snprintf(out, n, "USER%u", v);
        return;
    default:
        break;
    }
    snprintf(out, n, "?");
}

void keylabel_format(const char *code, char *out, size_t out_size)
{
    const char *slash = strchr(code, '/');
    if (!slash) {
        format_one(code, out, out_size);
        return;
    }
    /* タップ / ホールド：タップを上、ホールドを下に */
    char tap_code[24], tap[32], hold[32];
    size_t len = (size_t)(slash - code);
    if (len >= sizeof tap_code)
        len = sizeof tap_code - 1;
    memcpy(tap_code, code, len);
    tap_code[len] = '\0';
    format_one(tap_code, tap, sizeof tap);
    format_one(slash + 1, hold, sizeof hold);
    /* 2 行の表示を持つタップ（数字など）は 1 行目だけにする */
    char *nl = strchr(tap, '\n');
    snprintf(out, out_size, "%s\n%s", nl ? nl + 1 : tap, hold);
}
