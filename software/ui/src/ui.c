/*
 * タッチ UI の画面（試作）。
 *
 * 上：状態の帯（時計、物理キーの出力先、電源、レイヤー、CPU・メモリ・温度、RP2040 とのつながり）
 * 下：左のタブで切り替える
 *   Num    数字と記号のタッチキー
 *   F      F1〜F12、編集キー、矢印
 *   Media  メディアキーと、バックライトの明るさ
 *   System 物理キーの出力先、CPU のグラフ、RP2040 の UF2、電源を切る
 *
 * タッチキーは、押したときと離したときに UART で RP2040 に送る（docs/protocol.md の 1.3）。
 * 記号のラベルは US 配列のもの。PC 側が JIS 配列だと、Shift を使う記号は別の文字になる。
 */
#include "ui.h"

#include <stdio.h>
#include <time.h>

#include "lvgl.h"

#include "backlight.h"
#include "link.h"
#include "platform.h"
#include "sysinfo.h"

/* ---- HID の使用法（HID Usage Tables 1.5） ---- */

#define PAGE_KB       0x07
#define PAGE_CONSUMER 0x0C
#define MOD_LSHIFT    0x02      /* 修飾キーのビット。E0 + ビット位置が使用法 */

typedef struct {
    const char *label;          /* "\n" は行の区切り */
    uint8_t page;
    uint16_t usage;
    uint8_t mods;               /* いっしょに押す修飾キー（ビット 0 = LCtrl … 7 = RGui） */
} touch_key_t;

#define BR             { "\n", 0, 0, 0 }
#define K(l, u)        { (l), PAGE_KB, (u), 0 }
#define S(l, u)        { (l), PAGE_KB, (u), MOD_LSHIFT }
#define C(l, u)        { (l), PAGE_CONSUMER, (u), 0 }
#define COUNT(a)       (sizeof(a) / sizeof((a)[0]))

static const touch_key_t keys_num[] = {
    K("`", 0x35), K("1", 0x1E), K("2", 0x1F), K("3", 0x20), K("4", 0x21), K("5", 0x22),
    K("6", 0x23), K("7", 0x24), K("8", 0x25), K("9", 0x26), K("0", 0x27), K("-", 0x2D),
    K("=", 0x2E), K(LV_SYMBOL_BACKSPACE, 0x2A),
    BR,
    S("~", 0x35), S("!", 0x1E), S("@", 0x1F), S("#", 0x20), S("$", 0x21), S("%", 0x22),
    S("^", 0x23), S("&", 0x24), S("*", 0x25), S("(", 0x26), S(")", 0x27), S("_", 0x2D),
    S("+", 0x2E), K("Enter", 0x28),
    BR,
    K("[", 0x2F), K("]", 0x30), S("{", 0x2F), S("}", 0x30), K("\\", 0x31), S("|", 0x31),
    K(";", 0x33), S(":", 0x33), K("'", 0x34), S("\"", 0x34), S("<", 0x36), S(">", 0x37),
    S("?", 0x38), K("Del", 0x4C),
};

static const touch_key_t keys_fn[] = {
    K("F1", 0x3A), K("F2", 0x3B), K("F3", 0x3C), K("F4", 0x3D), K("F5", 0x3E), K("F6", 0x3F),
    K("F7", 0x40), K("F8", 0x41), K("F9", 0x42), K("F10", 0x43), K("F11", 0x44), K("F12", 0x45),
    BR,
    K("Esc", 0x29), K("Ins", 0x49), K("Del", 0x4C), K("Home", 0x4A), K("End", 0x4D),
    K("PgUp", 0x4B), K("PgDn", 0x4E), K("PrtSc", 0x46), K("Pause", 0x48),
    K(LV_SYMBOL_LEFT, 0x50), K(LV_SYMBOL_UP, 0x52), K(LV_SYMBOL_DOWN, 0x51), K(LV_SYMBOL_RIGHT, 0x4F),
};

static const touch_key_t keys_media[] = {
    C(LV_SYMBOL_PREV, 0xB6), C(LV_SYMBOL_PLAY " " LV_SYMBOL_PAUSE, 0xCD), C(LV_SYMBOL_STOP, 0xB7),
    C(LV_SYMBOL_NEXT, 0xB5), C(LV_SYMBOL_MUTE, 0xE2), C(LV_SYMBOL_VOLUME_MID " -", 0xEA),
    C(LV_SYMBOL_VOLUME_MAX " +", 0xE9),
};

/* ---- タッチキー（buttonmatrix 1 つで 1 面） ---- */

typedef struct {
    const touch_key_t *keys;
    size_t n_keys;
    const char **map;
    uint16_t *button_to_key;
    int pressed;                /* 押している keys の番号。なければ -1 */
} keypad_t;

static void send_mods(uint8_t mods, bool down)
{
    for (int bit = 0; bit < 8; bit++)
        if (mods & (1u << bit))
            link_send_key(PAGE_KB, (uint16_t)(0xE0 + bit), down);
}

static void key_down(const touch_key_t *k)
{
    send_mods(k->mods, true);
    link_send_key(k->page, k->usage, true);
}

static void key_up(const touch_key_t *k)
{
    link_send_key(k->page, k->usage, false);
    send_mods(k->mods, false);
}

static void keypad_event_cb(lv_event_t *e)
{
    keypad_t *kp = lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        /* CLICK_TRIG を付けていないので、押した瞬間に届く */
        uint32_t id = *(uint32_t *)lv_event_get_param(e);
        if (kp->pressed >= 0)
            key_up(&kp->keys[kp->pressed]);
        kp->pressed = kp->button_to_key[id];
        key_down(&kp->keys[kp->pressed]);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (kp->pressed >= 0) {
            key_up(&kp->keys[kp->pressed]);
            kp->pressed = -1;
        }
    } else if (code == LV_EVENT_DELETE) {
        lv_free(kp->map);
        lv_free(kp->button_to_key);
        lv_free(kp);
    }
}

static lv_obj_t *keypad_create(lv_obj_t *parent, const touch_key_t *keys, size_t n)
{
    keypad_t *kp = lv_malloc_zeroed(sizeof *kp);
    kp->keys = keys;
    kp->n_keys = n;
    kp->pressed = -1;
    kp->map = lv_malloc((n + 1) * sizeof(char *));
    kp->button_to_key = lv_malloc(n * sizeof(uint16_t));

    size_t buttons = 0;
    for (size_t i = 0; i < n; i++) {
        kp->map[i] = keys[i].label;
        if (keys[i].label[0] != '\n')
            kp->button_to_key[buttons++] = (uint16_t)i;
    }
    kp->map[n] = "";

    lv_obj_t *m = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(m, kp->map);
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_28, 0);
    lv_obj_add_event_cb(m, keypad_event_cb, LV_EVENT_ALL, kp);
    return m;
}

/* ---- 状態の帯 ---- */

static ui_actions_t actions;

static lv_obj_t *lbl_time, *lbl_date, *lbl_out, *lbl_pwr, *lbl_layer, *lbl_sys, *lbl_link;
static lv_obj_t *out_selector, *bl_slider, *bl_note, *cpu_chart;
static lv_chart_series_t *cpu_series;

static lv_obj_t *status_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, "");
    return l;
}

static void create_status_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), 64);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 24, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 40, 0);
    lv_obj_set_scrollable(bar, false);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lbl_time = status_label(bar, &lv_font_montserrat_40);
    lbl_date = status_label(bar, &lv_font_montserrat_20);

    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_set_height(spacer, 1);
    lv_obj_set_flex_grow(spacer, 1);

    lbl_out = status_label(bar, &lv_font_montserrat_20);
    lbl_pwr = status_label(bar, &lv_font_montserrat_20);
    lbl_layer = status_label(bar, &lv_font_montserrat_20);
    lbl_sys = status_label(bar, &lv_font_montserrat_20);
    lbl_link = status_label(bar, &lv_font_montserrat_20);
}

static void update_clock(void)
{
    struct tm tm;
    platform_localtime(time(NULL), &tm);
    char buf[32];
    strftime(buf, sizeof buf, "%H:%M", &tm);
    lv_label_set_text(lbl_time, buf);
    strftime(buf, sizeof buf, "%a %d %b %Y", &tm);
    lv_label_set_text(lbl_date, buf);
}

static void update_sysinfo(void)
{
    sysinfo_t s;
    sysinfo_read(&s);
    char cpu[8] = "--", temp[16] = "--";
    if (s.cpu_percent >= 0)
        snprintf(cpu, sizeof cpu, "%d%%", s.cpu_percent);
    if (s.temp_decic >= 0)
        snprintf(temp, sizeof temp, "%d.%d C", s.temp_decic / 10, s.temp_decic % 10);
    lv_label_set_text_fmt(lbl_sys, "CPU %s   RAM %d/%d MB   %s", cpu, s.mem_used_mb, s.mem_total_mb, temp);
    if (s.cpu_percent >= 0)
        lv_chart_set_next_value(cpu_chart, cpu_series, s.cpu_percent);
}

static void tick_1s(lv_timer_t *t)
{
    (void)t;
    update_clock();
    update_sysinfo();
}

/* ---- タブの中身 ---- */

static void bl_slider_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target_obj(e);
    backlight_set_level(lv_slider_get_value(s));
}

static void create_media_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(tab, 24, 0);

    lv_obj_t *pad = keypad_create(tab, keys_media, COUNT(keys_media));
    lv_obj_set_width(pad, LV_PCT(100));
    lv_obj_set_flex_grow(pad, 1);

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_size(row, LV_PCT(100), 80);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 32, 0);

    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, LV_SYMBOL_IMAGE "  Backlight");

    bl_slider = lv_slider_create(row);
    lv_slider_set_range(bl_slider, 0, 255);
    lv_slider_set_value(bl_slider, backlight_level(), LV_ANIM_OFF);
    lv_obj_set_flex_grow(bl_slider, 1);
    lv_obj_add_event_cb(bl_slider, bl_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    bl_note = lv_label_create(row);
    lv_label_set_text(bl_note, "");
}

static void out_selector_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target_obj(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(m);
    link_send_out(id == 1 ? LINK_OUT_PC : id == 2 ? LINK_OUT_CM0 : LINK_OUT_UNKNOWN);
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

static lv_obj_t *hold_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, LV_PCT(100), 110);
    lv_obj_add_event_cb(b, cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

static void create_system_tab(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tab, 32, 0);

    /* 物理キーの出力先 */
    lv_obj_t *col = lv_obj_create(tab);
    lv_obj_set_size(col, 480, LV_PCT(100));
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(col, false);
    lv_obj_t *l = lv_label_create(col);
    lv_label_set_text(l, LV_SYMBOL_KEYBOARD "  Physical keys to");
    static const char *out_map[] = { "Auto", "PC", "CM0", "" };
    out_selector = lv_buttonmatrix_create(col);
    lv_buttonmatrix_set_map(out_selector, out_map);
    lv_buttonmatrix_set_button_ctrl_all(out_selector, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(out_selector, true);
    lv_buttonmatrix_set_button_ctrl(out_selector, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_width(out_selector, LV_PCT(100));
    lv_obj_set_flex_grow(out_selector, 1);
    lv_obj_add_event_cb(out_selector, out_selector_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* CPU の使用率（直近 60 秒） */
    cpu_chart = lv_chart_create(tab);
    lv_obj_set_height(cpu_chart, LV_PCT(100));
    lv_obj_set_flex_grow(cpu_chart, 1);
    lv_chart_set_type(cpu_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(cpu_chart, 60);
    lv_chart_set_axis_range(cpu_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(cpu_chart, 5, 7);
    lv_chart_set_update_mode(cpu_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_style_size(cpu_chart, 0, 0, LV_PART_INDICATOR);
    cpu_series = lv_chart_add_series(cpu_chart, lv_palette_main(LV_PALETTE_CYAN), LV_CHART_AXIS_PRIMARY_Y);

    /* 押し続けると動くボタン */
    lv_obj_t *btns = lv_obj_create(tab);
    lv_obj_set_size(btns, 360, LV_PCT(100));
    lv_obj_set_flex_flow(btns, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(btns, false);
    hold_button(btns, LV_SYMBOL_DOWNLOAD "  RP2040 UF2 (hold)", uf2_cb);
    hold_button(btns, LV_SYMBOL_POWER "  Power off (hold)", poweroff_cb);
}

static lv_obj_t *add_keypad_tab(lv_obj_t *tv, const char *name, const touch_key_t *keys, size_t n)
{
    lv_obj_t *tab = lv_tabview_add_tab(tv, name);
    lv_obj_t *pad = keypad_create(tab, keys, n);
    lv_obj_set_size(pad, LV_PCT(100), LV_PCT(100));
    return tab;
}

void ui_create(const ui_actions_t *a)
{
    if (a)
        actions = *a;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(scr, false);

    create_status_bar(scr);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_obj_set_width(tv, LV_PCT(100));
    lv_obj_set_flex_grow(tv, 1);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_LEFT);
    lv_tabview_set_tab_bar_size(tv, 150);

    add_keypad_tab(tv, "Num", keys_num, COUNT(keys_num));
    add_keypad_tab(tv, "F", keys_fn, COUNT(keys_fn));
    create_media_tab(lv_tabview_add_tab(tv, "Media"));
    create_system_tab(lv_tabview_add_tab(tv, "System"));

    ui_show_link_state(link_state());
    update_clock();
    update_sysinfo();
    lv_timer_create(tick_1s, 1000, NULL);
}

void ui_show_link_state(const link_state_t *s)
{
    if (!lbl_link)
        return;     /* 画面を作る前 */

    lv_label_set_text(lbl_link, s->connected ? LV_SYMBOL_OK " RP2040" : LV_SYMBOL_WARNING " RP2040");

    const char *out = s->out == LINK_OUT_PC ? "PC" : s->out == LINK_OUT_CM0 ? "CM0"
                    : s->out == LINK_OUT_NONE ? "none" : "?";
    lv_label_set_text_fmt(lbl_out, LV_SYMBOL_KEYBOARD " %s%s", out,
                          s->out == LINK_OUT_UNKNOWN ? "" : s->out_manual ? " (manual)" : " (auto)");

    const char *cur = s->current == LINK_CURRENT_3A ? "3A" : s->current == LINK_CURRENT_1A5 ? "1.5A"
                    : s->current == LINK_CURRENT_DEFAULT ? "Default" : "?";
    if (s->port)
        lv_label_set_text_fmt(lbl_pwr, LV_SYMBOL_USB " %c %s", s->port, cur);
    else
        lv_label_set_text(lbl_pwr, LV_SYMBOL_USB " ?");

    if (s->layer >= 0)
        lv_label_set_text_fmt(lbl_layer, "Layer %d", s->layer);
    else
        lv_label_set_text(lbl_layer, "Layer ?");

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
