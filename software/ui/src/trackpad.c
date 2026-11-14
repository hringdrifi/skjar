#include "trackpad.h"

#include <math.h>
#include <stddef.h>

#define TAP_MAX_MS        200     /* これより短く触れて離したら「叩いた」 */
#define TAP_MAX_MOVE_PX   12.0f   /* 叩いたとみなす、動きの上限 */
#define DRAG_WINDOW_MS    300     /* 叩いてからこの間に触れて動かすとドラッグ */
#define DRAG_START_PX     6.0f
#define SCROLL_STEP_PX    24.0f   /* ホイールの 1 目盛りに当たる指の動き */
#define GAIN_BASE         1.2f    /* カーソルの速さ（指の px に対する倍率） */
#define GAIN_ACCEL        0.06f   /* 速く動かすほど遠くへ（1 回の動き 1 px ごとに足す倍率） */
#define GAIN_MAX          3.0f
#define JUMP_PX           100.0f  /* 1 回でこれより大きく動いたら、指の動きではなく位置の飛びとみなす */

static trackpad_output_t out;

static int prev_count;
static float prev_x, prev_y;
static uint32_t touch_start_ms;
static int max_count;           /* この触れ方で、いちばん多かった指の数 */
static float moved;             /* この触れ方で動いた距離 */
static uint32_t last_tap_ms;
static bool have_last_tap;
static bool drag_candidate;     /* 叩いた直後に触れた */
static bool dragging;
static uint8_t side_buttons;
static float acc_x, acc_y;      /* カーソルの端数 */
static float acc_v, acc_h;      /* スクロールの端数 */
static bool strip_touching;     /* スクロールの帯 */
static float strip_prev_y, strip_acc;

static uint8_t buttons_now(void)
{
    return (uint8_t)(side_buttons | (dragging ? TRACKPAD_BTN_LEFT : 0));
}

static int clamp127(int v)
{
    return v < -127 ? -127 : v > 127 ? 127 : v;
}

static void send_pointer(int dx, int dy)
{
    if (!out.pointer)
        return;
    /* 大きな動きは −127〜127 に分けて送る */
    do {
        int sx = clamp127(dx), sy = clamp127(dy);
        out.pointer(sx, sy, buttons_now(), out.user);
        dx -= sx;
        dy -= sy;
    } while (dx != 0 || dy != 0);
}

static void send_buttons(void)
{
    if (out.pointer)
        out.pointer(0, 0, buttons_now(), out.user);
}

static void click(uint8_t button)
{
    if (!out.pointer)
        return;
    out.pointer(0, 0, (uint8_t)(buttons_now() | button), out.user);
    out.pointer(0, 0, buttons_now(), out.user);
}

void trackpad_init(const trackpad_output_t *o)
{
    if (o)
        out = *o;
    trackpad_reset();
}

void trackpad_reset(void)
{
    bool had_buttons = buttons_now() != 0;
    prev_count = 0;
    max_count = 0;
    moved = 0;
    have_last_tap = false;
    drag_candidate = false;
    dragging = false;
    side_buttons = 0;
    acc_x = acc_y = acc_v = acc_h = 0;
    strip_touching = false;
    strip_acc = 0;
    if (had_buttons)
        send_buttons();
}

void trackpad_strip_update(bool touching, float y)
{
    if (!touching) {
        strip_touching = false;
        strip_acc = 0;
        return;
    }
    if (strip_touching) {
        float dy = y - strip_prev_y;
        if (fabsf(dy) <= JUMP_PX) {
            strip_acc += dy / SCROLL_STEP_PX;
            int iv = (int)strip_acc;
            strip_acc -= (float)iv;
            if (iv != 0 && out.wheel)
                out.wheel(clamp127(iv), 0, out.user);
        }
    }
    strip_touching = true;
    strip_prev_y = y;
}

void trackpad_set_side_buttons(uint8_t buttons)
{
    if (buttons == side_buttons)
        return;
    side_buttons = buttons;
    send_buttons();
}

static void move_pointer(float dx, float dy)
{
    float speed = sqrtf(dx * dx + dy * dy);
    float gain = GAIN_BASE + GAIN_ACCEL * speed;
    if (gain > GAIN_MAX)
        gain = GAIN_MAX;
    acc_x += dx * gain;
    acc_y += dy * gain;
    int ix = (int)acc_x, iy = (int)acc_y;   /* 0 に向けて切り捨て、端数は次に回す */
    acc_x -= (float)ix;
    acc_y -= (float)iy;
    if (ix != 0 || iy != 0)
        send_pointer(ix, iy);
}

static void scroll(float dx, float dy)
{
    /* 指と同じ向きに中身が動く（指を下へ → 中身が下へ → 上へスクロール） */
    acc_v += dy / SCROLL_STEP_PX;
    acc_h += -dx / SCROLL_STEP_PX;
    int iv = (int)acc_v, ih = (int)acc_h;
    acc_v -= (float)iv;
    acc_h -= (float)ih;
    if ((iv != 0 || ih != 0) && out.wheel)
        out.wheel(clamp127(iv), clamp127(ih), out.user);
}

static void touch_ended(uint32_t now_ms)
{
    uint32_t duration = now_ms - touch_start_ms;
    bool tap = duration <= TAP_MAX_MS && moved <= TAP_MAX_MOVE_PX;

    if (dragging) {
        dragging = false;
        send_buttons();
        have_last_tap = false;
    } else if (tap) {
        if (max_count >= 3)
            click(TRACKPAD_BTN_MIDDLE);
        else if (max_count == 2)
            click(TRACKPAD_BTN_RIGHT);
        else
            click(TRACKPAD_BTN_LEFT);
        have_last_tap = max_count == 1;
        last_tap_ms = now_ms;
    } else {
        have_last_tap = false;
    }
    drag_candidate = false;
    acc_x = acc_y = acc_v = acc_h = 0;
}

void trackpad_update(const trackpad_frame_t *f, uint32_t now_ms)
{
    int n = f->count;

    if (n == 0) {
        if (prev_count > 0)
            touch_ended(now_ms);
        prev_count = 0;
        return;
    }

    if (prev_count == 0) {
        /* 触れはじめ */
        touch_start_ms = now_ms;
        max_count = n;
        moved = 0;
        drag_candidate = have_last_tap && now_ms - last_tap_ms <= DRAG_WINDOW_MS;
        have_last_tap = false;
    } else if (n != prev_count) {
        /* 指が増えた・減った。重心が飛ぶので、この回は動かさない */
        if (n > max_count)
            max_count = n;
    } else if (fabsf(f->x - prev_x) > JUMP_PX || fabsf(f->y - prev_y) > JUMP_PX) {
        /* 触れた直後の古い位置や、ノイズ。動きには数えず、基準だけ置き直す */
    } else {
        float dx = f->x - prev_x, dy = f->y - prev_y;
        moved += fabsf(dx) + fabsf(dy);
        if (max_count >= 2) {
            /* 2 本指以上で触れたら、指が 1 本に減ってもカーソルは動かさない */
            if (n >= 2)
                scroll(dx, dy);
        } else {
            if (drag_candidate && !dragging && moved >= DRAG_START_PX) {
                dragging = true;
                send_buttons();
            }
            move_pointer(dx, dy);
        }
    }

    prev_count = n;
    prev_x = f->x;
    prev_y = f->y;
}
