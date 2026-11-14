/*
 * 画面の背景の画像（試し）。ボタンやパネルを半透明にして、背景が透けて見えるようにする。
 *
 * 画像は、PNG のファイルを渡せばそれを使い（1920 × 480 が描き換えが軽い。ほかの大きさは画面を覆う
 * ように拡大・縮小する）、渡さなければ「オーロラ」風の絵を起動時にプログラムで描く。
 */
#ifndef SKJAR_BACKGROUND_H
#define SKJAR_BACKGROUND_H

#include <stdbool.h>

#include "lvgl.h"

/* 背景の画像を、screen の一番奥に作る（初めは隠しておく）。png_path が NULL ならオーロラを描く */
void background_create(lv_obj_t *screen, const char *png_path);

/* 背景を出す・隠す */
void background_show(bool on);

#endif /* SKJAR_BACKGROUND_H */
