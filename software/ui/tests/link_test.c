/* link.c の行の組み立てと読み取りのテスト（LVGL を使わない） */
#include <stdio.h>
#include <string.h>

#include "../src/link.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int changed_count, shutdown_count;

static void on_changed(const link_state_t *s, void *u) { (void)s; (void)u; changed_count++; }
static void on_shutdown(void *u) { (void)u; shutdown_count++; }

int main(void)
{
    /* CRC-8（多項式 0x07、初期値 0）の確認値："123456789" → 0xF4 */
    CHECK(link_crc8("123456789", 9) == 0xF4);

    char line[80];
    size_t n = link_format(line, sizeof line, "ALIVE,0");
    CHECK(n > 0 && line[n - 1] == '\n');
    printf("example: %s", line);

    const link_callbacks_t cb = { .changed = on_changed, .shutdown = on_shutdown };
    link_open("-", &cb);
    CHECK(!link_state()->connected);

    /* 正しい行 */
    link_format(line, sizeof line, "PWR,B,1.5A");
    line[strlen(line) - 1] = '\0';
    CHECK(link_handle_line(line, 100));
    CHECK(link_state()->connected);
    CHECK(link_state()->port == 'B');
    CHECK(link_state()->current == LINK_CURRENT_1A5);

    /* 前にカーネルのログが付いていても読める */
    char mixed[128];
    link_format(line, sizeof line, "OUT,CM0,MAN");
    line[strlen(line) - 1] = '\0';
    snprintf(mixed, sizeof mixed, "[   12.345] usb 1-1: new device$SK,garbage%s", line);
    CHECK(link_handle_line(mixed, 200));
    CHECK(link_state()->out == LINK_OUT_CM0 && link_state()->out_manual);

    /* CRC が合わない行は捨てる */
    link_format(line, sizeof line, "LAYER,2");
    line[strlen(line) - 1] = '\0';
    line[5] = 'X';
    CHECK(!link_handle_line(line, 300));
    CHECK(link_state()->layer == -1);

    /* 範囲外の値は無視する */
    link_format(line, sizeof line, "BLMAX,300");
    line[strlen(line) - 1] = '\0';
    CHECK(link_handle_line(line, 400));
    CHECK(link_state()->bl_max == 255);

    /* 知らないコマンドは捨てるが、つながりの印にはなる */
    link_format(line, sizeof line, "FUTURE,1,2");
    line[strlen(line) - 1] = '\0';
    CHECK(link_handle_line(line, 500));

    link_format(line, sizeof line, "SHUTDOWN");
    line[strlen(line) - 1] = '\0';
    CHECK(link_handle_line(line, 600));
    CHECK(shutdown_count == 1);

    /* 3 秒届かなければ、つながっていないことにして、状態を消す */
    link_poll(600 + 3000);
    CHECK(!link_state()->connected);
    CHECK(link_state()->port == 0);

    link_close();
    if (failures == 0)
        printf("link_test: ok\n");
    return failures ? 1 : 0;
}
