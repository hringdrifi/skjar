/*
 * タッチ UI の画面（試作）。1920 × 480 を、ヘッダー（80 px）と下の面（400 px）に分ける。
 *
 * ヘッダー：時計、タブのボタン（Keys / Pad / Media / System）、状態の札（レイヤー、物理キーの出力先、
 *           電源、CPU と温度。RP2040 とつながっていないときだけ赤い札）
 * 下の面（物理キーに近い側）：開いているタブの中身
 *   Keys   2 段 × 12 キーのタッチキーを一番下に寄せる。物理キーの代わりに使う。割り当ては RMK のキーマップ
 *          （7〜10 行目）で、レイヤーで変わり、Vial で変えられる。押した・離したを番号で RP2040 に送る
 *   Pad    トラックパッド（trackpad.c。マルチタッチは touch_source.c）、スクロールの帯、L / M / R
 *   Media  メディアキー（固定）と、バックライトの明るさ
 *   System 物理キーの出力先、元のタブに戻る設定、CPU のグラフとメモリ、RP2040 の UF2、電源を切る
 *
 * タブは、上のボタンのほかに、Vial の USER0〜3 キー（Keys / Pad / Media / System）でも開ける。
 * 文字や機能のキーを押すと、設定によって元のタブに戻る（settings.h）。
 *
 * キーの大きさ：表示領域は 219.88 mm（TOUCH-A の図面）で 1920 px、1 px = 0.1145 mm。12 キーを
 * 19.05 mm（166 px）で並べると収まらないので、160 px（18.32 mm）で均等に並べる。真ん中は物理キーの
 * 列と合い、外側ほどずれる（両端で約 4 mm）。
 */
#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

#include "background.h"
#include "backlight.h"
#include "icons.h"
#include "keylabel.h"
#include "link.h"
#include "platform.h"
#include "settings.h"
#include "sysinfo.h"
#include "touch_source.h"
#include "trackpad.h"

#define SCREEN_W      1920
#define TOP_H         80      /* ヘッダー */
#define BODY_H        400     /* タブの中身 */
#define KEY_PITCH     160     /* 18.32 mm */
#define KEY_GAP       10
#define KEY_COLS      12
#define KEY_ROWS      2
#define TOUCH_KEYS    (KEY_COLS * KEY_ROWS)
#define MAX_LAYERS    8
#define CODE_LEN      24
#define LABEL_LEN     48

#define COUNT(a)      (sizeof(a) / sizeof((a)[0]))

enum { TAB_KEYS, TAB_PAD, TAB_MEDIA, TAB_SYSTEM, TAB_COUNT };

static ui_actions_t actions;

/* ---- 固定のキー（Media タブ。HID の使用法で送る） ---- */

#define PAGE_KB       0x07
#define PAGE_CONSUMER 0x0C

typedef struct {
    const char *label;
    uint8_t page;
    uint16_t usage;
} fixed_key_t;

#define C(l, u) { (l), PAGE_CONSUMER, (u) }

static const fixed_key_t keys_media[] = {
    C(LV_SYMBOL_PREV, 0xB6), C(LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, 0xCD), C(LV_SYMBOL_STOP, 0xB7),
    C(LV_SYMBOL_NEXT, 0xB5), C(LV_SYMBOL_MUTE, 0xE2), C(LV_SYMBOL_VOLUME_MID " -", 0xEA),
    C(LV_SYMBOL_VOLUME_MAX " +", 0xE9),
};

static int media_pressed = -1;

static void media_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED) {
        /* CLICK_TRIG を付けていないので、押した瞬間に届く */
        uint32_t id = *(uint32_t *)lv_event_get_param(e);
        if (media_pressed >= 0)
            link_send_key(keys_media[media_pressed].page, keys_media[media_pressed].usage, false);
        media_pressed = (int)id;
        link_send_key(keys_media[id].page, keys_media[id].usage, true);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && media_pressed >= 0) {
        link_send_key(keys_media[media_pressed].page, keys_media[media_pressed].usage, false);
        media_pressed = -1;
    }
}

/* ---- 上の帯（ヘッダー）：時計、タブのボタン、状態の札 ---- */

static lv_obj_t *lbl_time, *lbl_date, *lbl_ram;
static lv_obj_t *body;
static lv_obj_t *tab_bar;
static lv_obj_t *out_selector, *return_selector, *bl_slider, *bl_note, *cpu_chart;
static lv_chart_series_t *cpu_series;

/* 状態の札：角の丸い小さな箱に、色と文字。色で状態が分かるようにする */
typedef struct {
    lv_obj_t *box;
    lv_obj_t *label;
} chip_t;

static chip_t chip_layer, chip_out, chip_pwr, chip_sys, chip_link;
static lv_obj_t *lbl_cpu, *lbl_temp;    /* CPU の札の、数字の欄 */

#define COLOR_MUTED  lv_color_hex(0x3A3F47)
#define COLOR_ACCENT lv_palette_main(LV_PALETTE_ORANGE)
#define COLOR_OK     lv_palette_darken(LV_PALETTE_GREEN, 2)
#define COLOR_WARN   lv_palette_darken(LV_PALETTE_AMBER, 2)
#define COLOR_BAD    lv_palette_darken(LV_PALETTE_RED, 1)

static lv_obj_t *plain_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
}

/* ---- 見た目：背景の画像と、半透明のボタン（試し） ---- */

/* 背景を出すときに半透明にするところ。普段は、テーマの色（不透明）のまま */
typedef struct {
    lv_obj_t *obj;
    lv_style_selector_t sel;
    lv_opa_t opa;
} glass_t;

static glass_t glass_list[64];
static int glass_count;

static void glass(lv_obj_t *obj, lv_style_selector_t sel, lv_opa_t opa)
{
    if (glass_count < (int)COUNT(glass_list))
        glass_list[glass_count++] = (glass_t){ obj, sel, opa };
}

/* ボタンの並び（buttonmatrix）：枠は透明、ボタンは半透明、選んでいる・押しているボタンは濃いめ */
static void glass_matrix(lv_obj_t *m)
{
    glass(m, LV_PART_MAIN, LV_OPA_TRANSP);
    glass(m, LV_PART_ITEMS, 110);
    glass(m, LV_PART_ITEMS | LV_STATE_CHECKED, 230);
    glass(m, LV_PART_ITEMS | LV_STATE_PRESSED, 200);
}

static void apply_look(look_t look)
{
    bool picture = look == LOOK_PICTURE;
    background_show(picture);
    for (int i = 0; i < glass_count; i++) {
        if (picture)
            lv_obj_set_style_bg_opa(glass_list[i].obj, glass_list[i].opa, glass_list[i].sel);
        else
            lv_obj_remove_local_style_prop(glass_list[i].obj, LV_STYLE_BG_OPA, glass_list[i].sel);
    }
}

static chip_t chip(lv_obj_t *parent)
{
    chip_t c;
    c.box = lv_obj_create(parent);
    lv_obj_set_size(c.box, LV_SIZE_CONTENT, 48);
    lv_obj_set_scrollable(c.box, false);
    lv_obj_set_style_radius(c.box, 24, 0);
    lv_obj_set_style_border_width(c.box, 0, 0);
    lv_obj_set_style_pad_hor(c.box, 18, 0);
    lv_obj_set_style_pad_ver(c.box, 0, 0);
    lv_obj_set_style_bg_color(c.box, COLOR_MUTED, 0);
    c.label = text(c.box, &lv_font_montserrat_20);
    lv_obj_set_style_text_color(c.label, lv_color_white(), 0);
    lv_obj_center(c.label);
    return c;
}

static void chip_set(chip_t *c, lv_color_t color, const char *s)
{
    lv_obj_set_style_bg_color(c->box, color, 0);
    lv_label_set_text(c->label, s);
}

/* 文字の幅（px） */
static int32_t text_width(const char *s, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, s, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* 固定幅で右寄せの欄。数字が変わっても札の幅と文字の位置が動かないように、
 * いちばん幅の広い値（widest）が入る幅にする */
static lv_obj_t *fixed_field(lv_obj_t *parent, const char *widest)
{
    lv_obj_t *l = text(parent, &lv_font_montserrat_20);
    lv_obj_set_width(l, text_width(widest, &lv_font_montserrat_20));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    return l;
}

/* CPU の札：「CPU」「使用率」「温度」の 3 つの欄に分ける */
static chip_t cpu_chip(lv_obj_t *parent)
{
    chip_t c = chip(parent);
    lv_obj_set_flex_flow(c.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c.box, 8, 0);
    lv_label_set_text(c.label, "CPU");
    lbl_cpu = fixed_field(c.box, "100%");
    lbl_temp = fixed_field(c.box, "100C");
    return c;
}

static void show_tab(int tab, bool by_key);

static void tab_bar_cb(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id < TAB_COUNT)
        show_tab((int)id, false);
}

static void create_top(lv_obj_t *parent)
{
    lv_obj_t *top = lv_obj_create(parent);
    glass(top, LV_PART_MAIN, 110);
    lv_obj_set_size(top, SCREEN_W, TOP_H);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_hor(top, 24, 0);
    lv_obj_set_style_pad_ver(top, 0, 0);
    lv_obj_set_style_pad_column(top, 32, 0);
    lv_obj_set_scrollable(top, false);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 時計：時刻を大きく、日付をその下に小さく */
    lv_obj_t *clock = plain_box(top);
    lv_obj_set_size(clock, 200, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clock, LV_FLEX_FLOW_COLUMN);
    lbl_time = text(clock, &lv_font_montserrat_40);
    lbl_date = text(clock, &lv_font_montserrat_14);
    lv_obj_set_style_text_color(lbl_date, lv_palette_main(LV_PALETTE_GREY), 0);

    /* タブのボタン */
    static const char *tab_map[] = {
        LV_SYMBOL_KEYBOARD "  Keys", LV_SYMBOL_GPS "  Pad", LV_SYMBOL_AUDIO "  Media", LV_SYMBOL_SETTINGS "  System", "",
    };
    tab_bar = lv_buttonmatrix_create(top);
    lv_buttonmatrix_set_map(tab_bar, tab_map);
    lv_buttonmatrix_set_button_ctrl_all(tab_bar, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_buttonmatrix_set_one_checked(tab_bar, true);
    lv_buttonmatrix_set_button_ctrl(tab_bar, TAB_KEYS, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(tab_bar, 800, 72);
    lv_obj_set_style_pad_all(tab_bar, 4, 0);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_20, 0);
    lv_obj_add_event_cb(tab_bar, tab_bar_cb, LV_EVENT_VALUE_CHANGED, NULL);
    glass_matrix(tab_bar);

    /* 状態の札（右寄せ）：レイヤー、物理キーの出力先、電源、CPU と温度、RP2040 とのつながり */
    lv_obj_t *chips = plain_box(top);
    lv_obj_set_height(chips, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(chips, 1);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(chips, 12, 0);
    chip_link = chip(chips);
    chip_layer = chip(chips);
    chip_out = chip(chips);
    chip_pwr = chip(chips);
    chip_sys = cpu_chip(chips);
}

static void update_clock(void)
{
    struct tm tm;
    platform_localtime(time(NULL), &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%H:%M", &tm);
    lv_label_set_text(lbl_time, buf);
    strftime(buf, sizeof buf, "%a %d %b", &tm);
    lv_label_set_text(lbl_date, buf);
}

static void update_sysinfo(void)
{
    sysinfo_t s;
    sysinfo_read(&s);
    if (s.cpu_percent >= 0)
        lv_label_set_text_fmt(lbl_cpu, "%d%%", s.cpu_percent);
    else
        lv_label_set_text(lbl_cpu, "--");
    /* 温度がわからない（PC で試すときなど）ときも、欄は残して幅を変えない */
    if (s.temp_decic >= 0)
        lv_label_set_text_fmt(lbl_temp, "%dC", (s.temp_decic + 5) / 10);
    else
        lv_label_set_text(lbl_temp, "--");
    /* 熱いとき（70 °C 以上）は色で知らせる */
    lv_obj_set_style_bg_color(chip_sys.box, s.temp_decic >= 700 ? COLOR_WARN : COLOR_MUTED, 0);
    if (s.cpu_percent >= 0)
        lv_chart_set_next_value(cpu_chart, cpu_series, s.cpu_percent);
    if (lbl_ram && s.mem_total_mb > 0)
        lv_label_set_text_fmt(lbl_ram, "RAM %d / %d MB", s.mem_used_mb, s.mem_total_mb);
}

static void tick_1s(lv_timer_t *t)
{
    (void)t;
    update_clock();
    update_sysinfo();
}

/* ---- Keys：タッチキー ---- */

/* RP2040 から届いた割り当て（keylabel.h の符号）。届くまでは、ファームの既定の割り当て
 * （firmware/rp2040/src/keymap.rs の 7〜10 行目と同じ。PC で試すとき用）を表示する */
static char codes[MAX_LAYERS][TOUCH_KEYS][CODE_LEN];
static bool have_code[MAX_LAYERS][TOUCH_KEYS];
#define DEFAULT_LAYERS 3
static const char *const default_codes[DEFAULT_LAYERS][TOUCH_KEYS] = {
    {   /* 0：F1〜F12 / Esc 1〜0 Bksp */
        "K3A", "K3B", "K3C", "K3D", "K3E", "K3F", "K40", "K41", "K42", "K43", "K44", "K45",
        "K29", "K1E", "K1F", "K20", "K21", "K22", "K23", "K24", "K25", "K26", "K27", "K2A",
    },
    {   /* 1：移動・編集、空き、↑、ScrLk、Pause / 数字の段にない記号、空き、← ↓ →、Del */
        "K4A", "K4D", "K4B", "K4E", "K49", "K46", "K39", "K65", "-", "K52", "K47", "K48",
        "K35", "K2E", "K2F", "K30", "K31", "K33", "K34", "-", "K50", "K51", "K4F", "K4C",
    },
    {   /* 2：メディアと明るさ / タブを開く */
        "KA8", "KAA", "KA9", "KAC", "KAE", "KAB", "KBE", "KBD", "T", "T", "T", "T",
        "U0", "U1", "U2", "U3", "T", "T", "T", "T", "T", "T", "T", "T",
    },
};

static char labels[TOUCH_KEYS][LABEL_LEN];
static const char *keys_map[TOUCH_KEYS + 2];
static lv_obj_t *keys_panel, *keys_matrix;
static bool labels_dirty = true;
static uint32_t keys_down;          /* 押しているタッチキー（ビット i） */

/* 透過（T）は下のレイヤーを見る。RMK は「有効になっている下のレイヤー」を見るが、UI に届くのは
 * 一番上のレイヤーだけなので、今のレイヤーの次はレイヤー 0 を見る（スペースの長押しで 1 つだけ
 * レイヤーを重ねる、ふつうの使い方ではこれで合う） */
static const char *code_for(int layer, int i)
{
    const int order[2] = { layer, 0 };
    for (int k = 0; k < (layer > 0 ? 2 : 1); k++) {
        int l = order[k];
        const char *c = NULL;
        if (l < MAX_LAYERS && have_code[l][i])
            c = codes[l][i];
        else if (l < DEFAULT_LAYERS)
            c = default_codes[l][i];
        if (c && strcmp(c, "T") != 0)
            return c;
    }
    return "-";
}

static void refresh_labels(void)
{
    int layer = link_state()->layer;
    if (layer < 0)
        layer = 0;
    uint32_t empty = 0;     /* 何もしないキー（ビット i） */
    for (int i = 0; i < TOUCH_KEYS; i++) {
        keylabel_format(code_for(layer, i), labels[i], LABEL_LEN);
        /* buttonmatrix は空の文字 "" を並びの終わりとみなすので、空きのキーは空白 1 つにする */
        if (labels[i][0] == '\0') {
            snprintf(labels[i], LABEL_LEN, " ");
            empty |= 1u << i;
        }
    }
    lv_buttonmatrix_set_map(keys_matrix, keys_map);
    /* 印を付け直す（前のレイヤーの印が残らないよう、いったん消す）。空きのキーは薄く、押しているキーは押した色に */
    lv_buttonmatrix_clear_button_ctrl_all(keys_matrix, LV_BUTTONMATRIX_CTRL_DISABLED | LV_BUTTONMATRIX_CTRL_CHECKED);
    for (int i = 0; i < TOUCH_KEYS; i++) {
        if (empty & (1u << i))
            lv_buttonmatrix_set_button_ctrl(keys_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_DISABLED);
        if (keys_down & (1u << i))
            lv_buttonmatrix_set_button_ctrl(keys_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    labels_dirty = false;
}

void ui_set_touch_label(int layer, int index, const char *code)
{
    if (layer < 0 || layer >= MAX_LAYERS || index < 0 || index >= TOUCH_KEYS)
        return;
    snprintf(codes[layer][index], CODE_LEN, "%s", code);
    have_code[layer][index] = true;
    labels_dirty = true;    /* まとめて届くので、次の入力の周期でまとめて描き直す */
}

static void set_key_down(int i, bool down)
{
    uint32_t bit = 1u << i;
    if (((keys_down & bit) != 0) == down)
        return;
    if (down) {
        keys_down |= bit;
        lv_buttonmatrix_set_button_ctrl(keys_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED);
    } else {
        keys_down &= ~bit;
        lv_buttonmatrix_clear_button_ctrl(keys_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED);
    }
    link_send_touch_key(i, down);
}

static void release_all_keys(void)
{
    for (int i = 0; i < TOUCH_KEYS; i++)
        set_key_down(i, false);
}

/* 触れている指から、押しているキーを決める（マルチタッチなら同時押しできる） */
static void keys_input(const touch_contacts_t *c)
{
    lv_area_t a;
    lv_obj_get_coords(keys_matrix, &a);
    uint32_t now = 0;
    for (int k = 0; k < c->count; k++) {
        const lv_point_t *p = &c->points[k];
        if (p->x < a.x1 || p->x > a.x2 || p->y < a.y1 || p->y > a.y2)
            continue;
        int col = (p->x - a.x1) / KEY_PITCH;
        int row = (p->y - a.y1) / KEY_PITCH;
        if (col >= 0 && col < KEY_COLS && row >= 0 && row < KEY_ROWS)
            now |= 1u << (row * KEY_COLS + col);
    }
    for (int i = 0; i < TOUCH_KEYS; i++)
        set_key_down(i, (now & (1u << i)) != 0);
}

static void create_keys_panel(lv_obj_t *parent)
{
    keys_panel = plain_box(parent);
    lv_obj_set_size(keys_panel, SCREEN_W, BODY_H);

    for (int i = 0; i < TOUCH_KEYS; i++) {
        keys_map[i + (i >= KEY_COLS ? 1 : 0)] = labels[i];
    }
    keys_map[KEY_COLS] = "\n";
    keys_map[TOUCH_KEYS + 1] = "";

    /* キーは 2 段 × 160 px を面の一番下（物理キーに近い側）に寄せる。上の 80 px は、
     * ヘッダーのボタンとタッチキーの間の空き（押しまちがいよけ） */
    keys_matrix = lv_buttonmatrix_create(keys_panel);
    lv_obj_set_size(keys_matrix, SCREEN_W, KEY_ROWS * KEY_PITCH);
    lv_obj_align(keys_matrix, LV_ALIGN_BOTTOM_MID, 0, 0);
    /* 外側の余白 5 px ＋ 間 10 px で、キーは 150 px、ピッチは 160 px */
    lv_obj_set_style_pad_all(keys_matrix, KEY_GAP / 2, 0);
    lv_obj_set_style_pad_row(keys_matrix, KEY_GAP, 0);
    lv_obj_set_style_pad_column(keys_matrix, KEY_GAP, 0);
    lv_obj_set_style_border_width(keys_matrix, 0, 0);
    lv_obj_set_style_radius(keys_matrix, 0, 0);
    lv_obj_set_style_text_font(keys_matrix, &lv_font_montserrat_28, 0);
    /* 押した・離したは keys_input で指から決める（LVGL のポインタは 1 本分しか持たないため） */
    lv_obj_set_clickable(keys_matrix, false);
    glass_matrix(keys_matrix);
    /* 空きのキー（何もしない）は、目立たないよう、うっすら形だけ見せる */
    lv_obj_set_style_bg_opa(keys_matrix, LV_OPA_20, LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(keys_matrix, 0, LV_PART_ITEMS | LV_STATE_DISABLED);
    refresh_labels();
}

/* ---- Pad：トラックパッド ---- */

static lv_obj_t *pad_panel, *pad_area, *pad_strip, *pad_btn_left, *pad_btn_middle, *pad_btn_right;

static void tp_pointer(int dx, int dy, uint8_t buttons, void *user)
{
    (void)user;
    link_send_pointer(dx, dy, buttons);
}

static void tp_wheel(int v, int h, void *user)
{
    (void)user;
    link_send_wheel(v, h);
}

static bool point_in(const lv_obj_t *obj, const lv_point_t *p)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return p->x >= a.x1 && p->x <= a.x2 && p->y >= a.y1 && p->y <= a.y2;
}

/* パッドの上の指は動きに、帯の上の指はスクロールに、L / M / R の上の指はボタンにする
 * （マルチタッチなら同時に使える） */
static void pad_input(const touch_contacts_t *c)
{
    trackpad_frame_t f = { 0, 0, 0 };
    uint8_t side = 0;
    bool strip = false;
    float strip_y = 0;
    for (int i = 0; i < c->count; i++) {
        const lv_point_t *p = &c->points[i];
        if (point_in(pad_area, p)) {
            f.x += (float)p->x;
            f.y += (float)p->y;
            f.count++;
        } else if (point_in(pad_strip, p)) {
            if (!strip) {           /* 帯の指は 1 本だけ見る */
                strip = true;
                strip_y = (float)p->y;
            }
        } else if (point_in(pad_btn_left, p)) {
            side |= TRACKPAD_BTN_LEFT;
        } else if (point_in(pad_btn_middle, p)) {
            side |= TRACKPAD_BTN_MIDDLE;
        } else if (point_in(pad_btn_right, p)) {
            side |= TRACKPAD_BTN_RIGHT;
        }
    }
    if (f.count > 0) {
        f.x /= (float)f.count;
        f.y /= (float)f.count;
    }
    trackpad_set_side_buttons(side);
    trackpad_strip_update(strip, strip_y);
    trackpad_update(&f, lv_tick_get());
}

static lv_obj_t *pad_button(lv_obj_t *parent, const char *label)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    glass(b, LV_PART_MAIN, 150);
    return b;
}

static void create_pad_panel(lv_obj_t *parent)
{
    pad_panel = plain_box(parent);
    lv_obj_set_size(pad_panel, SCREEN_W, BODY_H);
    lv_obj_set_style_pad_all(pad_panel, 8, 0);
    lv_obj_set_style_pad_column(pad_panel, 24, 0);
    lv_obj_set_flex_flow(pad_panel, LV_FLEX_FLOW_ROW);

    pad_area = lv_obj_create(pad_panel);
    lv_obj_set_height(pad_area, LV_PCT(100));
    lv_obj_set_flex_grow(pad_area, 1);
    lv_obj_set_scrollable(pad_area, false);
    lv_obj_set_style_bg_color(pad_area, lv_palette_darken(LV_PALETTE_GREY, 4), 0);
    lv_obj_set_style_radius(pad_area, 16, 0);
    glass(pad_area, LV_PART_MAIN, 90);
    lv_obj_t *hint = lv_label_create(pad_area);
    lv_label_set_text(hint, "1 finger: move / tap: click / tap + drag: drag\n"
                            "2 fingers: scroll / tap: right click      3 fingers tap: middle click");
    lv_obj_set_style_text_color(hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* スクロールの帯（1 キー分の幅）。縦になぞるとスクロール */
    pad_strip = lv_obj_create(pad_panel);
    lv_obj_set_size(pad_strip, KEY_PITCH, LV_PCT(100));
    lv_obj_set_scrollable(pad_strip, false);
    lv_obj_set_style_bg_color(pad_strip, lv_palette_darken(LV_PALETTE_BLUE_GREY, 4), 0);
    lv_obj_set_style_radius(pad_strip, 16, 0);
    glass(pad_strip, LV_PART_MAIN, 110);
    lv_obj_t *arrows = lv_label_create(pad_strip);
    lv_label_set_text(arrows, LV_SYMBOL_UP "\n\nScroll\n\n" LV_SYMBOL_DOWN);
    lv_obj_set_style_text_align(arrows, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(arrows, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_center(arrows);

    /* L / M / R（1 キー分の幅） */
    lv_obj_t *col = plain_box(pad_panel);
    lv_obj_set_size(col, KEY_PITCH, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 12, 0);
    pad_btn_left = pad_button(col, "L");
    pad_btn_middle = pad_button(col, "M");
    pad_btn_right = pad_button(col, "R");

    static const trackpad_output_t out = { .pointer = tp_pointer, .wheel = tp_wheel };
    trackpad_init(&out);
}

/* ---- Media ---- */

static void bl_slider_cb(lv_event_t *e)
{
    backlight_set_level(lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void create_media_panel(lv_obj_t *parent, lv_obj_t **panel)
{
    *panel = plain_box(parent);
    lv_obj_set_size(*panel, SCREEN_W, BODY_H);
    lv_obj_set_style_pad_all(*panel, 12, 0);
    lv_obj_set_style_pad_row(*panel, 16, 0);
    lv_obj_set_flex_flow(*panel, LV_FLEX_FLOW_COLUMN);

    static const char *map[COUNT(keys_media) + 1];
    for (size_t i = 0; i < COUNT(keys_media); i++)
        map[i] = keys_media[i].label;
    map[COUNT(keys_media)] = "";
    lv_obj_t *m = lv_buttonmatrix_create(*panel);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_28, 0);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_set_flex_grow(m, 1);
    lv_obj_add_event_cb(m, media_event_cb, LV_EVENT_ALL, NULL);
    glass_matrix(m);

    lv_obj_t *row = lv_obj_create(*panel);
    lv_obj_set_size(row, LV_PCT(100), 80);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 32, 0);
    glass(row, LV_PART_MAIN, 90);
    /* 太陽のアイコン（LVGL の記号にないので icons.c で描く）と「Backlight」 */
    lv_obj_t *title = plain_box(row);
    lv_obj_set_size(title, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title, 10, 0);
    lv_obj_t *sun = icon_create(title, icon_sun(26), lv_color_white());
    lv_obj_t *l = lv_label_create(title);
    lv_label_set_text(l, "Backlight");
    lv_obj_set_style_image_recolor(sun, lv_obj_get_style_text_color(l, LV_PART_MAIN), 0);
    bl_slider = lv_slider_create(row);
    lv_slider_set_range(bl_slider, 0, 255);
    lv_slider_set_value(bl_slider, backlight_level(), LV_ANIM_OFF);
    lv_obj_set_flex_grow(bl_slider, 1);
    lv_obj_add_event_cb(bl_slider, bl_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    bl_note = lv_label_create(row);
    lv_label_set_text(bl_note, "");
}

/* ---- System ---- */

static void out_selector_cb(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    link_send_out(id == 1 ? LINK_OUT_PC : id == 2 ? LINK_OUT_CM0 : LINK_OUT_UNKNOWN);
}

static void return_selector_cb(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id <= RETURN_ALWAYS) {
        settings()->return_mode = (return_mode_t)id;
        settings_save();
    }
}

static void uf2_cb(lv_event_t *e)
{
    (void)e;
    link_send_uf2();
}

static void poweroff_cb(lv_event_t *e)
{
    (void)e;
    if (actions.poweroff)
        actions.poweroff();
}

static lv_obj_t *hold_button(lv_obj_t *parent, const char *label, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_width(b, LV_PCT(100));
    lv_obj_set_flex_grow(b, 1);
    lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    glass(b, LV_PART_MAIN, 150);
    return b;
}

static void look_selector_cb(lv_event_t *e)
{
    uint32_t id = lv_buttonmatrix_get_selected_button(lv_event_get_target_obj(e));
    if (id <= LOOK_PICTURE) {
        settings()->look = (look_t)id;
        settings_save();
        apply_look(settings()->look);
    }
}

/* 選ぶボタンの並びと見出し。icon を渡すと、見出しの前に描いたアイコン（icons.h）を置く
 * （渡さないときは、見出しの文字に LVGL の記号を入れて使う） */
static lv_obj_t *selector(lv_obj_t *parent, const lv_image_dsc_t *icon, const char *title, const char **map,
                          int width)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, width, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(col, false);
    glass(col, LV_PART_MAIN, 90);
    lv_obj_t *head = plain_box(col);
    lv_obj_set_size(head, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(head, 10, 0);
    lv_obj_t *img = icon ? icon_create(head, icon, lv_color_white()) : NULL;
    lv_obj_t *l = lv_label_create(head);
    lv_label_set_text(l, title);
    if (img)
        lv_obj_set_style_image_recolor(img, lv_obj_get_style_text_color(l, LV_PART_MAIN), 0);
    lv_obj_t *m = lv_buttonmatrix_create(col);
    lv_buttonmatrix_set_map(m, map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_buttonmatrix_set_one_checked(m, true);
    lv_obj_set_width(m, LV_PCT(100));
    lv_obj_set_flex_grow(m, 1);
    glass_matrix(m);
    return m;
}

static void create_system_panel(lv_obj_t *parent, lv_obj_t **panel)
{
    *panel = plain_box(parent);
    lv_obj_set_size(*panel, SCREEN_W, BODY_H);
    lv_obj_set_style_pad_all(*panel, 12, 0);
    lv_obj_set_style_pad_column(*panel, 24, 0);
    lv_obj_set_flex_flow(*panel, LV_FLEX_FLOW_ROW);

    static const char *out_map[] = { "Auto", "PC", "CM0", "" };
    out_selector = selector(*panel, NULL, LV_SYMBOL_KEYBOARD "  Physical keys to", out_map, 380);
    lv_buttonmatrix_set_button_ctrl(out_selector, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(out_selector, out_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);

    static const char *return_map[] = { "Never", "Key-opened", "Always", "" };
    return_selector = selector(*panel, NULL, LV_SYMBOL_LOOP "  Back to the previous tab on typing", return_map, 500);
    lv_buttonmatrix_set_button_ctrl(return_selector, (uint32_t)settings()->return_mode, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(return_selector, return_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 見た目（試し）：背景の画像と半透明のボタン */
    static const char *look_map[] = { "Plain", "Picture", "" };
    lv_obj_t *look_selector = selector(*panel, icon_palette(22), "Look", look_map, 260);
    lv_buttonmatrix_set_button_ctrl(look_selector, (uint32_t)settings()->look, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(look_selector, look_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* CPU の使用率（直近 60 秒）とメモリ */
    lv_obj_t *stats = plain_box(*panel);
    lv_obj_set_height(stats, LV_PCT(100));
    lv_obj_set_flex_grow(stats, 1);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stats, 8, 0);
    lv_obj_t *cpu_title = lv_label_create(stats);
    lv_label_set_text(cpu_title, "CPU (60 s)");
    cpu_chart = lv_chart_create(stats);
    lv_obj_set_width(cpu_chart, LV_PCT(100));
    lv_obj_set_flex_grow(cpu_chart, 1);
    glass(cpu_chart, LV_PART_MAIN, 90);
    lbl_ram = text(stats, &lv_font_montserrat_20);
    lv_chart_set_type(cpu_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(cpu_chart, 60);
    lv_chart_set_axis_range(cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(cpu_chart, 5, 7);
    lv_chart_set_update_mode(cpu_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(cpu_chart, 0, 0, LV_PART_INDICATOR);
    cpu_series = lv_chart_add_series(cpu_chart, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);

    /* 押し続けると動くボタン */
    lv_obj_t *btns = plain_box(*panel);
    lv_obj_set_size(btns, 320, LV_PCT(100));
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(btns, 16, 0);
    hold_button(btns, LV_SYMBOL_DOWNLOAD "  RP2040 UF2 (hold)", uf2_cb);
    hold_button(btns, LV_SYMBOL_POWER "  Power off (hold)", poweroff_cb);
}

/* ---- タブの切り替え ---- */

static lv_obj_t *panels[TAB_COUNT];
static int active_tab = TAB_KEYS;
static int return_tab = TAB_KEYS;   /* 戻る先 */
static bool opened_by_key;
/* タブを替えたら、触れている指がすべて離れるまで入力を読まない。替える前から触れていた指
 * （タブを開くタッチキーを押した指など）が、新しいタブで叩いたことにならないように */
static bool wait_release;

static void show_tab(int tab, bool by_key)
{
    if (tab < 0 || tab >= TAB_COUNT)
        return;
    if (tab == active_tab) {
        /* 開いているタブのキーをもう一度押したら、元のタブに戻る */
        if (by_key && tab != return_tab)
            show_tab(return_tab, false);
        return;
    }

    /* 離れるタブの、押しっぱなしを片付ける */
    if (active_tab == TAB_KEYS)
        release_all_keys();
    if (active_tab == TAB_PAD)
        trackpad_reset();

    return_tab = active_tab;
    opened_by_key = by_key;
    active_tab = tab;
    wait_release = true;
    for (int i = 0; i < TAB_COUNT; i++)
        lv_obj_set_hidden(panels[i], i != tab);
    lv_buttonmatrix_set_button_ctrl(tab_bar, (uint32_t)tab, LV_BUTTONMATRIX_CTRL_CHECKED);
}

void ui_user_key(int id, bool pressed)
{
    if (pressed && id >= 0 && id < TAB_COUNT)
        show_tab(id, true);
}

void ui_activity(void)
{
    /* Keys では戻らない（打っている最中なので） */
    if (active_tab == TAB_KEYS || active_tab == return_tab)
        return;
    return_mode_t mode = settings()->return_mode;
    if (mode == RETURN_ALWAYS || (mode == RETURN_KEY_OPENED && opened_by_key)) {
        int back = return_tab;
        show_tab(back, false);
        return_tab = TAB_KEYS;
    }
}

/* 10 ms ごと：開いているタブが指を使うものなら、指を読んで渡す */
static void input_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (labels_dirty)
        refresh_labels();
    if (active_tab != TAB_KEYS && active_tab != TAB_PAD)
        return;
    touch_contacts_t c;
    touch_source_read(&c);
    if (wait_release) {
        if (c.count > 0)
            return;
        wait_release = false;
    }
    if (active_tab == TAB_KEYS)
        keys_input(&c);
    else
        pad_input(&c);
}

void ui_create(const ui_actions_t *a)
{
    if (a)
        actions = *a;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 0, 0);

    create_top(scr);

    body = plain_box(scr);
    lv_obj_set_size(body, SCREEN_W, BODY_H);
    create_keys_panel(body);
    create_pad_panel(body);
    create_media_panel(body, &panels[TAB_MEDIA]);
    create_system_panel(body, &panels[TAB_SYSTEM]);
    panels[TAB_KEYS] = keys_panel;
    panels[TAB_PAD] = pad_panel;
    for (int i = 0; i < TAB_COUNT; i++)
        lv_obj_set_hidden(panels[i], i != TAB_KEYS);

    /* 背景の画像（画面の一番奥）。見た目の設定に合わせて出す */
    background_create(scr, actions.background_png);
    apply_look(settings()->look);

    touch_source_open(lv_obj_get_display(scr), NULL);
    lv_timer_create(input_timer_cb, 10, NULL);

    ui_show_link_state(link_state());
    update_clock();
    update_sysinfo();
    lv_timer_create(tick_1s, 1000, NULL);
}

void ui_show_link_state(const link_state_t *s)
{
    if (!chip_link.box)
        return;     /* 画面を作る前 */
    char buf[48];

    /* RP2040 とのつながり：切れているときだけ出す */
    chip_set(&chip_link, COLOR_BAD, LV_SYMBOL_WARNING "  RP2040");
    lv_obj_set_hidden(chip_link.box, s->connected);

    /* レイヤー：0 以外は目立つ色に（タッチキーの中身が変わっている） */
    if (s->layer >= 0)
        snprintf(buf, sizeof buf, "L%d", s->layer);
    else
        snprintf(buf, sizeof buf, "L?");
    chip_set(&chip_layer, s->layer > 0 ? COLOR_ACCENT : COLOR_MUTED, buf);
    labels_dirty = true;    /* レイヤーが変わるとタッチキーの表示も変わる */

    /* 物理キーの出力先：A は自動、M は手動 */
    const char *out = s->out == LINK_OUT_PC ? "PC" : s->out == LINK_OUT_CM0 ? "CM0"
                    : s->out == LINK_OUT_NONE ? "none" : "?";
    snprintf(buf, sizeof buf, LV_SYMBOL_KEYBOARD "  %s%s", out,
             s->out == LINK_OUT_UNKNOWN ? "" : s->out_manual ? "  M" : "  A");
    chip_set(&chip_out, s->out == LINK_OUT_NONE ? COLOR_WARN : COLOR_MUTED, buf);

    /* 電源：使っているポートと電流宣言。3A は緑、1.5A は黄、Default は赤 */
    const char *cur = s->current == LINK_CURRENT_3A ? "3A" : s->current == LINK_CURRENT_1A5 ? "1.5A"
                    : s->current == LINK_CURRENT_DEFAULT ? "Default" : "?";
    lv_color_t pc = s->current == LINK_CURRENT_3A ? COLOR_OK : s->current == LINK_CURRENT_1A5 ? COLOR_WARN
                  : s->current == LINK_CURRENT_DEFAULT ? COLOR_BAD : COLOR_MUTED;
    if (s->port)
        snprintf(buf, sizeof buf, LV_SYMBOL_CHARGE "  %c  %s", s->port, cur);
    else
        snprintf(buf, sizeof buf, LV_SYMBOL_CHARGE "  ?");
    chip_set(&chip_pwr, pc, buf);

    /* 出力先の選択：手動なら PC か CM0、自動なら Auto に印を付ける */
    if (s->out != LINK_OUT_UNKNOWN) {
        uint32_t id = !s->out_manual ? 0 : s->out == LINK_OUT_PC ? 1 : 2;
        lv_buttonmatrix_set_button_ctrl(out_selector, id, LV_BUTTONMATRIX_CTRL_CHECKED);
    }

    /* バックライトの上限 */
    backlight_set_limit(s->bl_max);
    if (s->bl_max < 255)
        lv_label_set_text_fmt(bl_note, "limit %d", s->bl_max);
    else
        lv_label_set_text(bl_note, "");
}
