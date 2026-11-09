/* keylabel.c（キーの動作の符号 → 表示）のテスト */
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "../src/keylabel.h"

static int failures;

static void expect(const char *code, const char *want)
{
    char got[64];
    keylabel_format(code, got, sizeof got);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s: got \"%s\", want \"%s\"\n", code, got, want);
        failures++;
    }
}

int main(void)
{
    expect("K3A", "F1");
    expect("K45", "F12");
    expect("K29", "Esc");
    expect("K1E", "!\n1");              /* 数字は Shift の記号を上に */
    expect("K27", ")\n0");
    expect("K2D", "_\n-");
    expect("K04", "A");
    expect("K2A", LV_SYMBOL_BACKSPACE);
    expect("K4A", "Home");
    expect("K50", LV_SYMBOL_LEFT);
    expect("KA8", LV_SYMBOL_MUTE);      /* RMK の AudioMute */
    expect("KE1", "Shift");
    expect("K1E+02", "!");              /* Shift ＋ 1 は「!」で見せる */
    expect("K06+01", "Ctrl+C");
    expect("M05", "Ctrl+Alt");
    expect("CE9", LV_SYMBOL_VOLUME_MAX);
    expect("L1", "L1");
    expect("G2", "TG2");
    expect("U0", "Keys");
    expect("U1", "Pad");
    expect("U3", "System");
    expect("U9", "USER9");
    expect("K2C/L1", "Space\nL1");      /* タップ / ホールド */
    expect("K1E/M02", "1\nShift");
    expect("-", "");
    expect("?", "?");

    if (failures == 0)
        printf("keylabel_test: ok\n");
    return failures ? 1 : 0;
}
