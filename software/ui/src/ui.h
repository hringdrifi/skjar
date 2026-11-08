/* タッチ UI の画面（1920 × 480 の横長） */
#ifndef SKJAR_UI_H
#define SKJAR_UI_H

#include "link.h"

typedef struct {
    void (*poweroff)(void);     /* 電源を切るボタンと、RP2040 からの SHUTDOWN */
} ui_actions_t;

void ui_create(const ui_actions_t *actions);

/* RP2040 から届いた状態を画面に出す（link の changed から呼ぶ） */
void ui_show_link_state(const link_state_t *state);

#endif /* SKJAR_UI_H */
