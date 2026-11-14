/*
 * RP2040 との UART の線（115200 bps、8N1、フロー制御なし、読み出しは待たない）。
 * Linux は tty（/dev/serial0 など）、Windows は COM ポート（COM5 など）。
 */
#ifndef SKJAR_SERIAL_PORT_H
#define SKJAR_SERIAL_PORT_H

#include <stdbool.h>
#include <stddef.h>

bool serial_open(const char *path);
bool serial_is_open(void);
/* 読めたバイト数。何もなければ 0、失敗なら -1 */
int serial_read(char *buf, size_t len);
/* 書けたバイト数。失敗なら -1 */
int serial_write(const char *buf, size_t len);
void serial_close(void);

#endif /* SKJAR_SERIAL_PORT_H */
