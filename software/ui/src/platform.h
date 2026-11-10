/* OS ごとに違う小物（時刻、待ち）。Linux と Windows */
#ifndef SKJAR_PLATFORM_H
#define SKJAR_PLATFORM_H

#include <stdint.h>
#include <time.h>

uint32_t platform_now_ms(void);         /* 単調増加のミリ秒 */
void platform_sleep_ms(uint32_t ms);
void platform_localtime(time_t t, struct tm *out);

/* 標準入力から 1 行読む（待たない）。読めたら true（改行は除く） */
#include <stdbool.h>
#include <stddef.h>
bool platform_read_stdin_line(char *buf, size_t size);

/* .wav を鳴らす（待たない）・止める。Linux は aplay、Windows は PlaySound。鳴らせなければ何もしない */
void platform_audio_play(const char *wav_path);
void platform_audio_stop(void);

/* Windows だけ：tools/ の Python の道具（exe の 1 つ上のフォルダの tools/。build-windows/ から見て）を、
 * 待たずに動かす（PATH の python。出力は UI と同じ端末へ）。動かせたら true。
 * Linux（実機）では何もしない（systemd のユニットが動かす）で false */
bool platform_run_tool(const char *script, const char *args);

#endif /* SKJAR_PLATFORM_H */
