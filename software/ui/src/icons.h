/*
 * LVGL の記号のフォントにないアイコンを、起動時にプログラムで描く（画像ファイルを持たないため）。
 * どれも A8（透明度だけ）の画像で、色は lv_image のスタイル image_recolor で付ける。
 */
#ifndef SKJAR_ICONS_H
#define SKJAR_ICONS_H

#include "lvgl.h"

/* 太陽（明るさ）。size は 1 辺の px（最大 64） */
const lv_image_dsc_t *icon_sun(int size);

/* パレット（見た目）。size は 1 辺の px（最大 64） */
const lv_image_dsc_t *icon_palette(int size);

/* 天気（Widget strip の Weather） */
typedef enum {
    ICON_WEATHER_CLEAR = 0,     /* 晴れ */
    ICON_WEATHER_PARTLY,        /* 晴れ時々曇り */
    ICON_WEATHER_CLOUDY,        /* 曇り */
    ICON_WEATHER_FOG,           /* 霧 */
    ICON_WEATHER_RAIN,          /* 雨（霧雨、にわか雨も） */
    ICON_WEATHER_SNOW,          /* 雪 */
    ICON_WEATHER_THUNDER,       /* 雷 */
    ICON_WEATHER_COUNT
} icon_weather_t;

/* size は 1 辺の px（最大 64）。大（48 以上）と小の 2 通りまで覚えておく */
const lv_image_dsc_t *icon_weather(icon_weather_t kind, int size);

/* WMO の天気のコード（Open-Meteo の weather_code）から、アイコンの種類 */
icon_weather_t icon_weather_from_wmo(int code);

/* アイコンの画像を作る。色は color（文字の色に合わせる） */
lv_obj_t *icon_create(lv_obj_t *parent, const lv_image_dsc_t *icon, lv_color_t color);

#endif /* SKJAR_ICONS_H */
