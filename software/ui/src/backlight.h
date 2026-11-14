/*
 * パネルのバックライト（/sys/class/backlight の最初のデバイス）。
 * 明るさは 0〜255 で扱い、デバイスの max_brightness に合わせて書く。
 * RP2040 から届く上限（BLMAX）を超えないようにする（docs/design-decisions.md の 2.4、2.5）。
 */
#ifndef SKJAR_BACKLIGHT_H
#define SKJAR_BACKLIGHT_H

#include <stdbool.h>

bool backlight_open(void);

/* 好みの明るさ（スライダーの値）と上限を受け、低い方を書き込む */
void backlight_set_level(int level);
void backlight_set_limit(int limit);

int backlight_level(void);
int backlight_effective(void);

#endif /* SKJAR_BACKLIGHT_H */
