/*
 * UI の設定。テキストのファイル（1 行に 1 つ、`名前=値`）に保存する。
 *   Linux   ：$XDG_CONFIG_HOME/skjar/ui.conf（なければ ~/.config/skjar/ui.conf）
 *   Windows ：%APPDATA%\skjar\ui.conf
 */
#ifndef SKJAR_SETTINGS_H
#define SKJAR_SETTINGS_H

/* キー入力で元のタブに戻るか */
typedef enum {
    RETURN_NEVER = 0,       /* 戻らない */
    RETURN_KEY_OPENED,      /* キー（USER キー）で開いたときだけ戻る */
    RETURN_ALWAYS,          /* いつでも戻る */
} return_mode_t;

typedef struct {
    return_mode_t return_mode;
} settings_t;

/* path が NULL なら上の場所を使う */
void settings_load(const char *path);
void settings_save(void);
settings_t *settings(void);

#endif /* SKJAR_SETTINGS_H */
