/* OS ごとに違う小物（時刻、待ち）。Linux と Windows */
#ifndef SKJAR_PLATFORM_H
#define SKJAR_PLATFORM_H

#include <stdint.h>
#include <time.h>

uint32_t platform_now_ms(void);         /* 単調増加のミリ秒 */
void platform_sleep_ms(uint32_t ms);
void platform_localtime(time_t t, struct tm *out);

#endif /* SKJAR_PLATFORM_H */
