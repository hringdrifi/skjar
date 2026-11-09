#include "link.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "serial_port.h"

#define LINE_MAX_LEN     64     /* docs/protocol.md 1.2 */
#define ALIVE_PERIOD_MS  1000
#define LINK_TIMEOUT_MS  3000
#define MAX_FIELDS       8

static bool dry_run;            /* 線を開かず、送る行を標準エラーに出す */
static link_callbacks_t cbs;
static link_state_t state;

static char rx_buf[LINE_MAX_LEN + 1];
static size_t rx_len;
static bool rx_overflow;        /* 長すぎる行は、次の改行まで捨てる */

static uint32_t last_rx_ms;
static uint32_t last_alive_ms;
static bool alive_started;
static uint8_t alive_seq;

uint8_t link_crc8(const char *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

size_t link_format(char *out, size_t out_size, const char *body)
{
    char payload[LINE_MAX_LEN];
    int n = snprintf(payload, sizeof payload, "SK,%s", body);
    if (n < 0 || (size_t)n >= sizeof payload)
        return 0;
    int m = snprintf(out, out_size, "$%s*%02X\n", payload, link_crc8(payload, (size_t)n));
    if (m < 0 || (size_t)m >= out_size || m > LINE_MAX_LEN)
        return 0;
    return (size_t)m;
}

static void send_body(const char *body)
{
    char line[LINE_MAX_LEN + 1];
    size_t len = link_format(line, sizeof line, body);
    if (len == 0) {
        fprintf(stderr, "link: message too long: %s\n", body);
        return;
    }
    if (dry_run) {
        fprintf(stderr, "link > %s", line);
        return;
    }
    if (!serial_is_open())
        return;
    int w = serial_write(line, len);
    if (w != (int)len)
        fprintf(stderr, "link: write failed (%d of %u bytes)\n", w, (unsigned)len);
}

static void notify_changed(void)
{
    if (cbs.changed)
        cbs.changed(&state, cbs.user);
}

static void reset_state(void)
{
    memset(&state, 0, sizeof state);
    state.bl_max = 255;
    state.layer = -1;
}

static bool parse_int(const char *s, int min, int max, int *out)
{
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < min || v > max)
        return false;
    *out = (int)v;
    return true;
}

static void handle_message(char **f, int n)
{
    const char *cmd = f[0];
    bool changed = false;

    if (strcmp(cmd, "ALIVE") == 0) {
        /* つながりの判定は handle_line で済ませている */
    } else if (strcmp(cmd, "PWR") == 0 && n == 3) {
        char port = (strcmp(f[1], "A") == 0) ? 'A' : (strcmp(f[1], "B") == 0) ? 'B' : 0;
        link_current_t cur = strcmp(f[2], "3A") == 0   ? LINK_CURRENT_3A
                           : strcmp(f[2], "1.5A") == 0 ? LINK_CURRENT_1A5
                           : strcmp(f[2], "DEF") == 0  ? LINK_CURRENT_DEFAULT
                                                       : LINK_CURRENT_UNKNOWN;
        state.port = port;
        state.current = cur;
        changed = true;
    } else if (strcmp(cmd, "BLMAX") == 0 && n == 2) {
        int v;
        if (parse_int(f[1], 0, 255, &v)) {
            state.bl_max = v;
            changed = true;
        }
    } else if (strcmp(cmd, "OUT") == 0 && n == 3) {
        state.out = strcmp(f[1], "PC") == 0    ? LINK_OUT_PC
                  : strcmp(f[1], "CM0") == 0   ? LINK_OUT_CM0
                  : strcmp(f[1], "NONE") == 0  ? LINK_OUT_NONE
                                               : LINK_OUT_UNKNOWN;
        state.out_manual = strcmp(f[2], "MAN") == 0;
        changed = true;
    } else if (strcmp(cmd, "LAYER") == 0 && n == 2) {
        int v;
        if (parse_int(f[1], 0, 31, &v)) {
            state.layer = v;
            changed = true;
        }
    } else if (strcmp(cmd, "SHUTDOWN") == 0 && n == 1) {
        if (cbs.shutdown)
            cbs.shutdown(cbs.user);
    }
    /* 知らないコマンドは捨てる（1.2） */

    if (changed)
        notify_changed();
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool link_handle_line(const char *line, uint32_t now_ms)
{
    /* 行の途中にカーネルのログが割り込んでも拾えるよう、最後の "$SK," から読む */
    const char *start = NULL;
    for (const char *p = strstr(line, "$SK,"); p; p = strstr(p + 1, "$SK,"))
        start = p;
    if (!start)
        return false;

    const char *star = strrchr(start, '*');
    if (!star || strlen(star) != 3)
        return false;
    int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    if (hi < 0 || lo < 0)
        return false;
    size_t payload_len = (size_t)(star - (start + 1));
    if (link_crc8(start + 1, payload_len) != (uint8_t)(hi << 4 | lo))
        return false;

    /* "SK," の後ろを , で分ける */
    char body[LINE_MAX_LEN + 1];
    size_t body_len = payload_len - 3;
    if (body_len == 0 || body_len > LINE_MAX_LEN)
        return false;
    memcpy(body, start + 4, body_len);
    body[body_len] = '\0';

    char *fields[MAX_FIELDS];
    int n = 0;
    for (char *p = body; n < MAX_FIELDS;) {
        fields[n++] = p;
        char *comma = strchr(p, ',');
        if (!comma)
            break;
        *comma = '\0';
        p = comma + 1;
    }
    if (fields[0][0] == '\0')
        return false;

    last_rx_ms = now_ms;
    if (!state.connected) {
        /* つながった（RP2040 が再起動した場合も含む）。今の状態を送り直してもらう */
        state.connected = true;
        send_body("HELLO");
        notify_changed();
    }
    handle_message(fields, n);
    return true;
}

static void feed(const char *data, size_t len, uint32_t now_ms)
{
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (c == '\r')
            continue;
        if (c == '\n') {
            if (!rx_overflow) {
                rx_buf[rx_len] = '\0';
                link_handle_line(rx_buf, now_ms);
            }
            rx_len = 0;
            rx_overflow = false;
            continue;
        }
        if (rx_len >= LINE_MAX_LEN) {
            rx_overflow = true;
            continue;
        }
        rx_buf[rx_len++] = c;
    }
}

bool link_open(const char *path, const link_callbacks_t *cb)
{
    if (cb)
        cbs = *cb;
    reset_state();
    rx_len = 0;
    rx_overflow = false;
    alive_started = false;

    if (!path || strcmp(path, "-") == 0) {
        dry_run = true;
        fprintf(stderr, "link: no serial port, printing messages instead\n");
    } else if (!serial_open(path)) {
        return false;
    }
    send_body("HELLO");
    notify_changed();
    return true;
}

void link_close(void)
{
    serial_close();
    dry_run = false;
}

void link_poll(uint32_t now_ms)
{
    if (serial_is_open()) {
        char buf[256];
        for (;;) {
            int r = serial_read(buf, sizeof buf);
            if (r > 0) {
                feed(buf, (size_t)r, now_ms);
                continue;
            }
            if (r < 0)
                fprintf(stderr, "link: read failed\n");
            break;
        }
    }

    if (!alive_started || now_ms - last_alive_ms >= ALIVE_PERIOD_MS) {
        char body[16];
        snprintf(body, sizeof body, "ALIVE,%u", alive_seq++);
        send_body(body);
        last_alive_ms = now_ms;
        alive_started = true;
    }

    if (state.connected && now_ms - last_rx_ms >= LINK_TIMEOUT_MS) {
        reset_state();      /* わからない状態に戻す（古い値を表示し続けない） */
        notify_changed();
    }
}

const link_state_t *link_state(void)
{
    return &state;
}

void link_send_key(uint8_t page, uint16_t usage, bool down)
{
    char body[32];
    snprintf(body, sizeof body, "KEY,%02X,%X,%d", page, usage, down ? 1 : 0);
    send_body(body);
}

void link_send_out(link_out_t target)
{
    const char *s = target == LINK_OUT_PC ? "PC" : target == LINK_OUT_CM0 ? "CM0" : "AUTO";
    char body[16];
    snprintf(body, sizeof body, "OUT,%s", s);
    send_body(body);
}

void link_send_lcd(bool on)
{
    send_body(on ? "LCD,1" : "LCD,0");
}

void link_send_uf2(void)
{
    send_body("UF2");
}
