/*
 * LVGL の設定（LVGL 9.6）。書いていない項目は LVGL の初期値（lv_conf_internal.h）になる。
 *
 * 描画先は CMake の SKJAR_UI_BACKEND で選ぶ：
 *   fbdev ... 実機（CM0、試作では Pi Zero 2 W）。/dev/fb0 に描き、タッチは evdev から読む
 *   sdl   ... PC で試すための窓（Linux / WSL）
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 32

/* メモリと文字列は C の標準ライブラリを使う（Linux で動かすので、LVGL の内蔵のヒープは要らない） */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_THEME_DEFAULT_DARK 1

/* 文字は英数字だけにしている（Montserrat に日本語の字形はない） */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT LV_FONT_DEFAULT_MONTSERRAT_20

#if defined(SKJAR_UI_SDL)
    #define LV_USE_SDL 1
#else
    #define LV_USE_LINUX_FBDEV 1
    /* 画面を回す（480 × 1920 → 1920 × 480）ので、変わったところだけを描いて、その部分だけを回して書き込む */
    #define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
    #define LV_LINUX_FBDEV_BUFFER_COUNT 0
    #define LV_LINUX_FBDEV_BUFFER_SIZE 120
    #define LV_USE_EVDEV 1
#endif

#endif /* LV_CONF_H */
