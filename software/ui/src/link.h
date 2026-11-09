/*
 * RP2040 との UART のやり取り（docs/protocol.md の 1 章）。
 *
 * 1 行 1 メッセージ：$SK,<コマンド>[,<引数>...]*<CRC-8 の 16 進 2 桁>\n
 * カーネルのログが混ざるので、この形でない行は捨てる。
 */
#ifndef SKJAR_LINK_H
#define SKJAR_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 物理キーの出力先（RP2040 が OUT で知らせる） */
typedef enum {
    LINK_OUT_UNKNOWN = 0,
    LINK_OUT_PC,
    LINK_OUT_CM0,
    LINK_OUT_NONE,
} link_out_t;

/* 使用中のポートの電流宣言（RP2040 が PWR で知らせる） */
typedef enum {
    LINK_CURRENT_UNKNOWN = 0,
    LINK_CURRENT_DEFAULT,
    LINK_CURRENT_1A5,
    LINK_CURRENT_3A,
} link_current_t;

typedef struct {
    bool connected;         /* RP2040 から 3 秒以内に正しい行が届いている */
    char port;              /* 'A' か 'B'。わからないときは 0 */
    link_current_t current;
    int bl_max;             /* バックライトの上限（0〜255）。初期値は 255（上限なし） */
    link_out_t out;
    bool out_manual;
    int layer;              /* わからないときは -1 */
} link_state_t;

typedef struct {
    void (*changed)(const link_state_t *state, void *user);  /* 状態が変わった */
    void (*shutdown)(void *user);                            /* SHUTDOWN を受けた */
    /* TKL：タッチキー index（0〜23）の、レイヤー layer での割り当て（keylabel.h の符号） */
    void (*touch_label)(int layer, int index, const char *code, void *user);
    void (*user_key)(int id, bool pressed, void *user);     /* USER：Vial の USER キー */
    void (*activity)(void *user);                            /* ACT：文字や機能のキーが押された */
    void *user;
} link_callbacks_t;

/* path を開く。NULL か "-" なら線を開かず、送る行を標準エラーに出すだけにする（PC で試すとき） */
bool link_open(const char *path, const link_callbacks_t *cb);
void link_close(void);

/* 定期的に呼ぶ（10 ms ごとくらい）。受信の処理と、ALIVE の送信、つながりの判定をする */
void link_poll(uint32_t now_ms);

const link_state_t *link_state(void);

/* 画面のタッチキー（0〜23。RP2040 がキーマップの 7〜10 行目として RMK に渡す） */
void link_send_touch_key(int index, bool down);
/* 固定のキー（Media タブなど）。page は 0x07（キーボード）か 0x0C（Consumer） */
void link_send_key(uint8_t page, uint16_t usage, bool down);
/* トラックパッド：dx / dy / v / h は −127〜127。buttons はビット 0 左、1 右、2 中 */
void link_send_pointer(int dx, int dy, uint8_t buttons);
void link_send_wheel(int v, int h);
void link_send_out(link_out_t target_or_unknown_for_auto);
void link_send_lcd(bool on);
void link_send_uf2(void);

/* CRC-8（多項式 0x07、初期値 0x00、反転なし） */
uint8_t link_crc8(const char *data, size_t len);

/* 送る行を組み立てる（テストとツール用）。body は "SK," の後ろ。行の長さを返す。入らなければ 0 */
size_t link_format(char *out, size_t out_size, const char *body);

/* 受けた 1 行（改行を除く）を処理する。正しい行なら true（テスト用にも公開） */
bool link_handle_line(const char *line, uint32_t now_ms);

#endif /* SKJAR_LINK_H */
