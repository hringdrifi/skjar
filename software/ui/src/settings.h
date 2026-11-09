/*
 * UI の設定。テキストのファイル（1 行に 1 つ、`名前=値`）に保存する。
 *   Linux   ：$XDG_CONFIG_HOME/skjar/ui.conf（なければ ~/.config/skjar/ui.conf）
 *   Windows ：%APPDATA%\skjar\ui.conf
 */
#ifndef SKJAR_SETTINGS_H
#define SKJAR_SETTINGS_H

#include <stdbool.h>

/* キー入力で元のタブに戻るか */
typedef enum {
    RETURN_NEVER = 0,       /* 戻らない */
    RETURN_KEY_OPENED,      /* キー（USER キー）で開いたときだけ戻る */
    RETURN_ALWAYS,          /* いつでも戻る */
} return_mode_t;

/* 画面の見た目（試し） */
typedef enum {
    LOOK_PLAIN = 0,         /* 背景なし、ボタンは不透明 */
    LOOK_PICTURE,           /* 背景の画像（background.h）と、半透明のボタン */
} look_t;

/* ウィジェット（widgets.h の widget_id_t の順。0 の None はいつも出す） */
#define SETTINGS_WIDGETS 5

typedef struct {
    return_mode_t return_mode;
    look_t look;
    bool widgets[SETTINGS_WIDGETS];     /* System タブで有効にしたウィジェット。初期値はどれも無効 */
    int shown_widget;                   /* Widget strip に出していたもの（次の起動でも同じものから） */
} settings_t;

/* path が NULL なら上の場所を使う */
void settings_load(const char *path);
void settings_save(void);
/* true なら、settings_save で書かない（録画のとき。台本で変えた設定を残さない） */
void settings_set_readonly(bool readonly);
settings_t *settings(void);

/* 設定のファイルを置くフォルダ（settings_load のあと。隠し機能の動画もここに置く） */
const char *settings_dir(void);

#endif /* SKJAR_SETTINGS_H */
