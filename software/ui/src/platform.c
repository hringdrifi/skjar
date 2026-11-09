#include "platform.h"

#if defined(_WIN32)

#include <windows.h>

uint32_t platform_now_ms(void)
{
    return (uint32_t)GetTickCount64();
}

void platform_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

void platform_localtime(time_t t, struct tm *out)
{
    localtime_s(out, &t);
}

#else

#include <unistd.h>

uint32_t platform_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void platform_sleep_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

void platform_localtime(time_t t, struct tm *out)
{
    localtime_r(&t, out);
}

#endif
