/*
 * 隠し機能：動画を全画面で流し、キーのライトも合わせて光らせる（ヘッダーの時計を 5 回叩くと始まる）。
 *
 * 動画は tools/make_movie.py で作った .skm（画面とキーのライトのコマ）と、同じ名前の .wav（音）。
 * 画面は 320 × 240、4 段階の濃淡を縦横 2 倍にして、真ん中に 640 × 480 で出す。左側には 42 キーの
 * 並びを小さく描き、キーのライトを見せる（PC で試すとき用。実機ではキーも光る）。
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
