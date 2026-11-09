/*
 * LVGL の設定（LVGL 9.6）。書いていない項目は LVGL の初期値（lv_conf_internal.h）になる。
 *
 * 描画先は CMake の SKJAR_UI_BACKEND で選ぶ：
 *   fbdev   ... 実機（CM0、試作では Pi Zero 2 W）。/dev/fb0 に描き、タッチは evdev から読む
 *   sdl     ... PC で試すための窓（Linux / WSL）
 *   windows ... PC で試すための窓（Windows。Win32）
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_XRGB8888

/* メモリと文字列は C の標準ライブラリを使う（Linux で動かすので、LVGL の内蔵のヒープは要らない） */
#define LV_USE_STDLIB_MALLOC  LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING  LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#if defined(SKJAR_UI_WINDOWS)
    /* Windows のドライバは、窓の処理を別のスレッドで動かすので、LVGL の OS の層が要る */
    #define LV_USE_OS LV_OS_WINDOWS
#else
    #define LV_USE_OS LV_OS_NONE
#endif

#define LV_USE_LOG    1
#define LV_LOG_LEVEL  LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_THEME_DEFAULT_DARK 1

/* 背景の画像（background.c）：PNG を読み、展開したものを覚えておく（毎回展開し直さない） */
#define LV_USE_LODEPNG 1
#define LV_USE_FS_STDIO 1
#define LV_FS_STDIO_LETTER 'A'
#define LV_CACHE_DEF_SIZE (8 * 1024 * 1024)

/* 文字はほぼ英数字だけ（Montserrat に日本語の字形はない） */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_DEFAULT LV_FONT_DEFAULT_MONTSERRAT_20

/* 日本語：ニュースの電光掲示板だけ、TTF のフォントを起動時に読んで描く（widgets.c）。
 * ファイルは丸ごとメモリに読む（数 MB〜20 MB）。描いた字は覚えておき、流すたびに描き直さない */
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 512

/* 録画（recorder.c）：画面を 1 コマずつ写す */
#define LV_USE_SNAPSHOT 1

#if defined(SKJAR_UI_SDL)
    #define LV_USE_SDL 1
#elif defined(SKJAR_UI_WINDOWS)
    #define LV_USE_WINDOWS 1
#else
    #define LV_USE_LINUX_FBDEV 1
    /* 画面を回す（480 × 1920 → 1920 × 480）ので、変わったところだけを描いて、その部分だけを回して書き込む */
    #define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_PARTIAL
    #define LV_LINUX_FBDEV_BUFFER_COUNT 0
    #define LV_LINUX_FBDEV_BUFFER_SIZE 120
    #define LV_USE_EVDEV 1
#endif

#endif /* LV_CONF_H */
