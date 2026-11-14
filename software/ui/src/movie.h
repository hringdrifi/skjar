/*
 * 隠し機能：動画を全画面で流し、キーのライトも合わせて光らせる（ヘッダーの時計を 5 回叩くと始まる）。
 *
 * 動画は tools/make_movie.py で作った .skm（画面とキーのライトのコマ）と、同じ名前の .wav（音）。
 * スクリーンとキーボードを 1 枚の画面に見立てて、動画を上下に分けて映す（分け方は make_movie.py）。
 * 画面のコマは 960 × 240、4 段階の濃淡を縦横 2 倍にして、画面いっぱいに出す（古い 320 × 240 の
 * ものは真ん中に出す）。RP2040 とつながっていないとき（PC で試すとき）は、左下に 42 キーの並びを
 * 小さく重ねて、キーのライトを見せる。
 * キーのライトは、コマごとに UART の LEDF で RP2040 に送る。画面のどこかを叩くと止まる。
 */
#ifndef SKJAR_MOVIE_H
#define SKJAR_MOVIE_H

#include <stdbool.h>

/* path の .skm を読み、再生を始める。読めなければ false（理由は標準エラーに出す） */
bool movie_start(const char *path);
void movie_stop(void);
bool movie_playing(void);

#endif /* SKJAR_MOVIE_H */
