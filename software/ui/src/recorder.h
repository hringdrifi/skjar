/*
 * 録画：台本（テキスト）どおりに UI を動かし、画面（1920 × 480）を 1 コマずつ ffmpeg に渡して動画にする。
 * デモなどの紹介の動画を、マウスを使わずに、何度でも同じように撮るため（tools/demo/、tools/make_demo.py）。
 *
 * 時計は録画の中だけで進める（1 コマごとに 1/fps 秒）。PC が遅くても、動画の中の動きは決まった速さになる。
 * 画面は lv_snapshot で写し、一番上の層（知らせの文字など）を重ねる。ffmpeg は PATH にあるものを使う。
 *
 * 台本：1 行に 1 つ、`<秒> <命令> [引数]`。# から行の終わりまでは注。
 *   link <行>            RP2040 から届いたことにする（LAYER,1、USER,1,1、ACT、PWR,A,3A など）
 *   type <回数> <秒>     ACT（文字のキー）を、続く秒のあいだに等しい間隔で送る
 *   touch <番号> <1|0>   タッチキーを押した・離した（0〜23。上の段が 0〜11）
 *   tab <keys|pad|media|system>
 *   look <plain|picture>
 *   swipe <1|-1>         Widget strip を右の隣へ・左の隣へ
 *   pomodoro <1|0>       ポモドーロの ▶ / ⏸ を押した・離した
 *   end                  ここで録画を終える
 */
#ifndef SKJAR_RECORDER_H
#define SKJAR_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

/* 台本を読み、時計を録画の時計にする（ui_create の前に呼ぶ） */
bool recorder_open(const char *script_path, const char *out_path, int fps);

/* 録画する。終わるまで戻らない。うまくいけば 0 */
int recorder_run(void);

#endif /* SKJAR_RECORDER_H */
