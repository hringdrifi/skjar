/* タッチ UI の画面（1920 × 480 の横長） */
#ifndef SKJAR_UI_H
#define SKJAR_UI_H

#include <stdbool.h>

#include "link.h"

typedef struct {
    void (*poweroff)(void);     /* 電源を切るボタンと、RP2040 からの SHUTDOWN */
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

#endif /* SKJAR_UI_H */
