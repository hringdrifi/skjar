/* trackpad.c の判定のテスト（LVGL を使わない） */
#include <stdio.h>
#include <string.h>

#include "../src/trackpad.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* 送られたものを記録する */
static int sum_dx, sum_dy, sum_v, sum_h, pointer_calls;
static uint8_t last_buttons;
static int presses[8];      /* ボタンのビットごとの、押された回数 */

static void on_pointer(int dx, int dy, uint8_t buttons, void *u)
{
    (void)u;
    sum_dx += dx;
    sum_dy += dy;
    for (int b = 0; b < 3; b++)
        if ((buttons & (1 << b)) && !(last_buttons & (1 << b)))
            presses[b]++;
    last_buttons = buttons;
    pointer_calls++;
}

static void on_wheel(int v, int h, void *u)
{
    (void)u;
    sum_v += v;
    sum_h += h;
}

static void clear(void)
{
    sum_dx = sum_dy = sum_v = sum_h = pointer_calls = 0;
    memset(presses, 0, sizeof presses);
}

static uint32_t now = 1000;

static void frame(int count, float x, float y, uint32_t step_ms)
{
    trackpad_frame_t f = { count, x, y };
    trackpad_update(&f, now);
    now += step_ms;
}

int main(void)
{
    const trackpad_output_t out = { on_pointer, on_wheel, NULL };
    trackpad_init(&out);

    /* 1 本指で右下へ動かすと、カーソルも右下へ動く。ボタンは押さない */
    clear();
    for (int i = 0; i <= 20; i++)
        frame(1, 100.0f + i * 5, 100.0f + i * 3, 10);
    frame(0, 0, 0, 500);
    CHECK(sum_dx > 100 && sum_dy > 60);
    CHECK(presses[0] == 0 && presses[1] == 0);
    CHECK(last_buttons == 0);

    /* 1 本指で叩くと左クリック */
    clear();
    frame(1, 300, 200, 50);
    frame(1, 301, 200, 50);
    frame(0, 0, 0, 500);
    CHECK(presses[0] == 1 && presses[1] == 0);
    CHECK(last_buttons == 0);
    CHECK(sum_dx <= 2 && sum_dy == 0);

    /* 2 本指で叩くと右クリック */
    clear();
    frame(1, 300, 200, 10);
    frame(2, 320, 200, 50);
    frame(2, 320, 201, 50);
    frame(0, 0, 0, 500);
    CHECK(presses[1] == 1 && presses[0] == 0);

    /* 3 本指で叩くと中クリック */
    clear();
    frame(3, 300, 200, 50);
    frame(0, 0, 0, 500);
    CHECK(presses[2] == 1);

    /* 2 本指を下へ動かすと上へスクロール（中身が指と同じ向きに動く）。カーソルは動かない */
    clear();
    for (int i = 0; i <= 20; i++)
        frame(2, 400, 100.0f + i * 10, 10);
    frame(0, 0, 0, 500);
    CHECK(sum_v >= 7);
    CHECK(sum_dx == 0 && sum_dy == 0);
    CHECK(presses[1] == 0);

    /* 2 本指から 1 本に減っても、カーソルは動かない */
    clear();
    frame(2, 400, 100, 10);
    frame(2, 400, 110, 10);
    for (int i = 0; i <= 10; i++)
        frame(1, 400.0f + i * 10, 110, 10);
    frame(0, 0, 0, 500);
    CHECK(sum_dx == 0);

    /* 叩いてすぐ触れて動かすと、左ボタンを押したまま動く。離すと左ボタンも離れる */
    clear();
    frame(1, 500, 200, 50);
    frame(0, 0, 0, 100);            /* 叩いた（クリック 1 回） */
    frame(1, 500, 200, 10);
    for (int i = 1; i <= 10; i++)
        frame(1, 500.0f + i * 5, 200, 10);
    CHECK(last_buttons & TRACKPAD_BTN_LEFT);
    CHECK(presses[0] == 2);         /* クリックの 1 回と、ドラッグの始まり */
    frame(0, 0, 0, 500);
    CHECK(!(last_buttons & TRACKPAD_BTN_LEFT));

    /* 長く触れてから動かさずに離したものは、クリックにしない */
    clear();
    frame(1, 600, 200, 300);
    frame(1, 600, 200, 10);
    frame(0, 0, 0, 500);
    CHECK(presses[0] == 0);

    /* 横の L ボタン */
    clear();
    trackpad_set_side_buttons(TRACKPAD_BTN_LEFT);
    CHECK(last_buttons == TRACKPAD_BTN_LEFT);
    trackpad_set_side_buttons(0);
    CHECK(last_buttons == 0);

    /* 速い動きで 127 を超えたら、−127〜127 に分けて送る（90 px × 倍率 3 = 270） */
    clear();
    frame(1, 0, 0, 10);
    frame(1, 90, 0, 10);
    frame(0, 0, 0, 500);
    CHECK(pointer_calls >= 3);
    CHECK(sum_dx >= 260);

    /* 触れた直後に位置が大きく飛んだ分は、動きに数えない（2 本指のスクロールも同じ） */
    clear();
    frame(2, 1000, 100, 10);
    frame(2, 400, 100, 10);         /* 古い位置からの飛び */
    for (int i = 1; i <= 10; i++)
        frame(2, 400, 100.0f + i * 10, 10);
    frame(0, 0, 0, 500);
    CHECK(sum_h == 0);
    CHECK(sum_v >= 3);

    /* スクロールの帯：1 本指で下へなぞると上へスクロール（2 本指と同じ向き）。カーソルは動かない */
    clear();
    for (int i = 0; i <= 12; i++)
        trackpad_strip_update(true, 100.0f + i * 10);
    trackpad_strip_update(false, 0);
    CHECK(sum_v >= 4 && sum_v <= 5);    /* 120 px ÷ 24 px（端数の丸めで 4 もありうる） */
    CHECK(sum_dx == 0 && sum_dy == 0);

    if (failures == 0)
        printf("trackpad_test: ok\n");
    return failures ? 1 : 0;
}
