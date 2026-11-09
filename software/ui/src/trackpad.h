/*
 * トラックパッドの判定。触れている指の数と、その重心から、マウスの動きとボタンを作る。
 * LVGL にも OS にも依存しない（tests/trackpad_test.c で試す）。
 *
 *   1 本指で動かす          カーソルを動かす
 *   1 本指で叩く            左クリック
 *   叩いてすぐ触れて動かす  左ボタンを押したまま動かす（ドラッグ）
 *   2 本指で動かす          スクロール（指と同じ向きに中身が動く）
 *   2 本指で叩く            右クリック
 *   3 本指で叩く            中クリック
 */
#ifndef SKJAR_TRACKPAD_H
#define SKJAR_TRACKPAD_H

#include <stdbool.h>
#include <stdint.h>

#define TRACKPAD_BTN_LEFT   0x01
#define TRACKPAD_BTN_RIGHT  0x02
#define TRACKPAD_BTN_MIDDLE 0x04

/* ある時点の、パッドに触れている指 */
typedef struct {
    int count;          /* 指の数（0 なら離れている） */
    float x, y;         /* 重心（画面の px） */
} trackpad_frame_t;

typedef struct {
    void (*pointer)(int dx, int dy, uint8_t buttons, void *user);  /* dx / dy は −127〜127 */
    void (*wheel)(int v, int h, void *user);                       /* v は正で上、h は正で右 */
    void *user;
} trackpad_output_t;

void trackpad_init(const trackpad_output_t *out);

/* 10 ms ごとくらいに呼ぶ */
void trackpad_update(const trackpad_frame_t *frame, uint32_t now_ms);

/* 画面の横の L / R ボタン（押している間ずっと立てる） */
void trackpad_set_side_buttons(uint8_t buttons);

/* ボタンをすべて離し、途中の判定を捨てる（タブを離れたときなど） */
void trackpad_reset(void);

#endif /* SKJAR_TRACKPAD_H */
