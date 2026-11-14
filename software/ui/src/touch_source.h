/*
 * トラックパッド用に、画面に触れている指をすべて読む（LVGL のポインタは 1 本分しか持たないため）。
 *
 * Linux（fbdev）：タッチパネルの evdev をもう一度開き、マルチタッチ（MT プロトコル B）を読む。
 *   座標は LVGL と同じく、パネルの向きから画面の回転に合わせて回す。
 * それ以外（Windows、SDL）と、マルチタッチのデバイスが見つからないとき：LVGL のポインタを 1 本の指にする。
 *   Windows では Shift を押している間 2 本、Ctrl を押している間 3 本の指として扱う（マウスで試すため）。
 */
#ifndef SKJAR_TOUCH_SOURCE_H
#define SKJAR_TOUCH_SOURCE_H

#include <stdbool.h>

#include "lvgl.h"

#define TOUCH_MAX_CONTACTS 10

typedef struct {
    int count;
    lv_point_t points[TOUCH_MAX_CONTACTS];  /* 画面の座標 */
} touch_contacts_t;

/* path が NULL なら、マルチタッチのデバイスを探す（Linux のみ） */
void touch_source_open(lv_display_t *disp, const char *path);
void touch_source_read(touch_contacts_t *out);

#endif /* SKJAR_TOUCH_SOURCE_H */
