/*
 * LVGL の記号のフォントにないアイコンを、起動時にプログラムで描く（画像ファイルを持たないため）。
 * どれも A8（透明度だけ）の画像で、色は lv_image のスタイル image_recolor で付ける。
 */
#ifndef SKJAR_ICONS_H
#define SKJAR_ICONS_H

#include "lvgl.h"

/* 太陽（明るさ）。size は 1 辺の px（最大 64） */
const lv_image_dsc_t *icon_sun(int size);

/* アイコンの画像を作る。色は color（文字の色に合わせる） */
lv_obj_t *icon_create(lv_obj_t *parent, const lv_image_dsc_t *icon, lv_color_t color);

#endif /* SKJAR_ICONS_H */
