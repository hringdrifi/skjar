/*
 * RP2040 が送るキーの動作の符号（docs/protocol.md の 1.4 の TKL）を、タッチキーの表示にする。
 *
 *   K<hh>         HID のキーコード（RMK の HidKeyCode。0x04〜0xE7 は HID の使用法と同じ）
 *   K<hh>+<mm>    修飾キー付きのキー（mm は修飾キーのビット。0 = LCtrl … 7 = RGui）
 *   C<hhh> S<hh>  Consumer / System Control
 *   M<mm>         修飾キーだけ
 *   L<n> G<n> D<n> O<n>  レイヤー（押している間 / 切り替え / 既定 / 1 回だけ）
 *   U<n>          USER キー（U0〜U3 はタブを開く）
 *   <tap>/<hold>  タップとホールドで違うキー
 *   -             何もしない   T  透過（下のレイヤーを見る）   ?  表示できない種類
 */
#ifndef SKJAR_KEYLABEL_H
#define SKJAR_KEYLABEL_H

#include <stddef.h>

/* 表示の文字列を out に書く。2 行にするときは '\n' で区切る */
void keylabel_format(const char *code, char *out, size_t out_size);

#endif /* SKJAR_KEYLABEL_H */
