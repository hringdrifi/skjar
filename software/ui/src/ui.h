/* タッチ UI の画面（1920 × 480 の横長） */
#ifndef SKJAR_UI_H
#define SKJAR_UI_H

#include <stdbool.h>

#include "link.h"

typedef struct {
    void (*poweroff)(void);     /* 電源を切るボタンと、RP2040 からの SHUTDOWN */
    void (*open_console)(void); /* サイバーデックモードへ：UI を閉じて、コンソールに戻る */
    const char *background_png;     /* 背景の画像（PNG）。NULL なら描いた絵を使う（background.h） */
    const char *movie_path;         /* 隠し機能の動画（.skm。movie.h） */
    const char *font_path;          /* 日本語のフォント（ニュースの電光掲示板）。NULL なら探す（widgets.h） */
} ui_actions_t;

void ui_create(const ui_actions_t *actions);

/* RP2040 から届いた状態を画面に出す（link の changed から呼ぶ） */
void ui_show_link_state(const link_state_t *state);

/* RP2040 から届いた、タッチキーの割り当て（link の touch_label から呼ぶ） */
void ui_set_touch_label(int layer, int index, const char *code);

/* USER キー（link の user_key から）：U0〜U3 でタブを開く */
void ui_user_key(int id, bool pressed);

/* 文字や機能のキーが押された（link の activity から）：設定によって元のタブに戻る */
void ui_activity(void);

/* 録画（recorder.c）から：台本どおりに見せる操作。ui_demo_start のあとは、指を読まない */
void ui_demo_start(void);
void ui_demo_tab(int tab);                      /* 0 Keys、1 Pad、2 Media、3 System */
void ui_demo_touch_key(int index, bool down);   /* タッチキーを押した・離した（見た目と TK） */
void ui_demo_look(int look);                    /* 0 Plain、1 Picture（保存しない） */

#endif /* SKJAR_UI_H */
