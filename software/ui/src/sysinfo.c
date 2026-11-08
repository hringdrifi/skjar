#include "sysinfo.h"

#include <stdio.h>
#include <string.h>

static unsigned long long prev_total, prev_idle;
static int have_prev;

static int read_cpu(void)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return -1;
    unsigned long long user, nice, sys, idle, iowait, irq, softirq, steal;
    int n = fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n != 8)
        return -1;

    unsigned long long idle_all = idle + iowait;
    unsigned long long total = user + nice + sys + idle + iowait + irq + softirq + steal;
    int percent = -1;
    if (have_prev && total > prev_total) {
        unsigned long long dt = total - prev_total;
        unsigned long long di = idle_all - prev_idle;
        percent = (int)((dt - di) * 100 / dt);
    }
    prev_total = total;
    prev_idle = idle_all;
    have_prev = 1;
    return percent;
}

static void read_mem(int *used_mb, int *total_mb)
{
    *used_mb = *total_mb = -1;
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return;
    char key[32];
    unsigned long kb, total = 0, avail = 0;
    int found = 0;
    while (fscanf(f, "%31s %lu kB", key, &kb) == 2) {
        if (strcmp(key, "MemTotal:") == 0) { total = kb; found |= 1; }
        else if (strcmp(key, "MemAvailable:") == 0) { avail = kb; found |= 2; }
        if (found == 3)
            break;
    }
    fclose(f);
    if (found == 3) {
        *total_mb = (int)(total / 1024);
        *used_mb = (int)((total - avail) / 1024);
    }
}

static int read_temp(void)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f)
        return -1;
    long milli;
    int n = fscanf(f, "%ld", &milli);
    fclose(f);
    return n == 1 ? (int)(milli / 100) : -1;
}

void sysinfo_read(sysinfo_t *out)
{
    out->cpu_percent = read_cpu();
    read_mem(&out->mem_used_mb, &out->mem_total_mb);
    out->temp_decic = read_temp();
}
