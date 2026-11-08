/* CPU・メモリ・温度を /proc と /sys から読む */
#ifndef SKJAR_SYSINFO_H
#define SKJAR_SYSINFO_H

typedef struct {
    int cpu_percent;    /* 前回の呼び出しからの使用率。初回とわからないときは -1 */
    int mem_used_mb;    /* MemTotal - MemAvailable。わからないときは -1 */
    int mem_total_mb;
    int temp_decic;     /* SoC の温度（0.1 °C 単位）。わからないときは -1 */
} sysinfo_t;

void sysinfo_read(sysinfo_t *out);

#endif /* SKJAR_SYSINFO_H */
