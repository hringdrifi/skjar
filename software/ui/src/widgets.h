/*
 * ウィジェット：Keys タブの、ヘッダーとタッチキーの間の帯（1920 × 80 px）に出す小物。
 * 左右にスワイプして切り替える（輪になっていて、端から反対の端へつながる）。None（何も出さない）の右に、System タブで
 * 有効にしたものが並ぶ。
 *   Pomodoro ：ポモドーロ・タイマー（集中 25 分、休憩 5 分、4 回ごとに長い休憩 15 分）
 *   News     ：ニュースの電光掲示板。見出しは設定のフォルダの news.txt（1 行に 1 つ）から読む。
 *              取ってくるのは別の道具（tools/fetch_news.py。実機では systemd のタイマーで回す）
 *
 * 指は ui.c の Keys タブの入力（マルチタッチ）から受け取る。帯の中のボタンも、LVGL のクリックでは
 * なく、ここで指から押した・離したを決める（スワイプの途中でボタンが押されないように）。
 */
#ifndef SKJAR_WIDGETS_H
#define SKJAR_WIDGETS_H

#include <stdbool.h>

#include "lvgl.h"
#include "touch_source.h"

typedef enum {
    WIDGET_NONE = 0,
    WIDGET_POMODORO,
    WIDGET_NEWS,
    WIDGET_COUNT
} widget_id_t;

typedef struct {
    /* ボタンの見た目を、ほかのボタンと揃える（ui.c の press_look と glass） */
    void (*style_button)(lv_obj_t *button);
    /* 画面に短い知らせを出す（ui.c の toast） */
    void (*notify)(const char *msg);
    /* ニュースの電光掲示板の日本語のフォント（TTF / OTF / TTC）。NULL なら、決まった場所を探す：
     *   Linux   ：IPA ゴシック（apt の fonts-ipafont-gothic）、Noto Sans CJK（fonts-noto-cjk）
     *   Windows ：BIZ UDゴシック、游ゴシック、メイリオ
     * 見つからなければ英数字のフォント（Montserrat）で、ASCII の見出しだけを出す */
    const char *font_path;
} widgets_hooks_t;

/* strip（1920 × 80 の箱）の中にウィジェットを作る */
void widgets_create(lv_obj_t *strip, const widgets_hooks_t *hooks);

/* 有効なウィジェットが変わった（System タブ）：並びを作り直す */
void widgets_refresh(void);

/* 触れている指（帯の外の指も含めて渡してよい） */
void widgets_input(const touch_contacts_t *contacts);

/* 指を読まなくなるとき（タブを替えた、動画が始まった）：押しているボタンとスワイプを片付ける */
void widgets_release(void);

/* System タブに出す名前 */
const char *widgets_name(widget_id_t id);

#endif /* SKJAR_WIDGETS_H */
