#include "recorder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "link.h"
#include "settings.h"
#include "ui.h"
#include "widgets.h"

#if defined(_WIN32)
#define POPEN(c) _popen((c), "wb")
#define PCLOSE(p) _pclose(p)
#else
#define POPEN(c) popen((c), "w")
#define PCLOSE(p) pclose(p)
#endif

#define SCREEN_W   1920
#define SCREEN_H   480
#define MAX_EVENTS 1024

typedef struct {
    uint32_t ms;
    char cmd[16];
    char arg[96];
} event_t;

static event_t events[MAX_EVENTS];
static int event_count;
static uint32_t end_ms;
static uint32_t clock_ms;   /* 録画の中の時計 */
static int fps = 30;
static char out_path[600];

static uint32_t recorder_tick(void)
{
    return clock_ms;
}

static void add(uint32_t ms, const char *cmd, const char *arg)
{
    if (event_count >= MAX_EVENTS) {
        fprintf(stderr, "record: too many events\n");
        return;
    }
    /* 時刻の順に並べる（同じ時刻なら書いた順） */
    int i = event_count++;
    while (i > 0 && events[i - 1].ms > ms) {
        events[i] = events[i - 1];
        i--;
    }
    events[i].ms = ms;
    snprintf(events[i].cmd, sizeof events[i].cmd, "%s", cmd);
    snprintf(events[i].arg, sizeof events[i].arg, "%s", arg);
}

bool recorder_open(const char *script_path, const char *out, int frames_per_second)
{
    FILE *f = fopen(script_path, "r");
    if (!f) {
        fprintf(stderr, "record: cannot open %s\n", script_path);
        return false;
    }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = '\0';
        float sec;
        char cmd[16], arg[96] = "";
        int got = sscanf(line, "%f %15s %95[^\r\n]", &sec, cmd, arg);
        if (got < 2)
            continue;
        uint32_t ms = (uint32_t)(sec * 1000.0f + 0.5f);
        if (strcmp(cmd, "end") == 0) {
            end_ms = ms;
        } else if (strcmp(cmd, "type") == 0) {
            int count = 0;
            float span = 0;
            if (sscanf(arg, "%d %f", &count, &span) != 2 || count <= 0) {
                fprintf(stderr, "record: line %d: type <count> <seconds>\n", n);
                continue;
            }
            for (int i = 0; i < count; i++)
                add(ms + (uint32_t)(span * 1000.0f * (float)i / (float)count), "link", "ACT");
        } else {
            add(ms, cmd, arg);
        }
    }
    fclose(f);
    if (!end_ms)
        end_ms = event_count ? events[event_count - 1].ms + 1000 : 5000;

    fps = frames_per_second > 0 ? frames_per_second : 30;
    snprintf(out_path, sizeof out_path, "%s", out);
    clock_ms = 0;
    lv_tick_set_cb(recorder_tick);
    settings_set_readonly(true);
    fprintf(stderr, "record: %d events, %.1f s at %d fps -> %s\n", event_count, end_ms / 1000.0, fps, out_path);
    return true;
}

static int name_index(const char *s, const char *const *names, int count)
{
    for (int i = 0; i < count; i++)
        if (strcmp(s, names[i]) == 0)
            return i;
    return -1;
}

static void run_event(const event_t *e)
{
    static const char *const tabs[] = { "keys", "pad", "media", "system" };
    static const char *const looks[] = { "plain", "picture" };
    if (strcmp(e->cmd, "link") == 0) {
        link_inject(e->arg, clock_ms);
    } else if (strcmp(e->cmd, "touch") == 0) {
        int index, down;
        if (sscanf(e->arg, "%d %d", &index, &down) == 2)
            ui_demo_touch_key(index, down != 0);
    } else if (strcmp(e->cmd, "tab") == 0) {
        int t = name_index(e->arg, tabs, 4);
        if (t >= 0)
            ui_demo_tab(t);
    } else if (strcmp(e->cmd, "look") == 0) {
        int l = name_index(e->arg, looks, 2);
        if (l >= 0)
            ui_demo_look(l);
    } else if (strcmp(e->cmd, "swipe") == 0) {
        widgets_demo_swipe(atoi(e->arg));
    } else if (strcmp(e->cmd, "pomodoro") == 0) {
        widgets_demo_pomodoro(atoi(e->arg) != 0);
    } else {
        fprintf(stderr, "record: unknown command %s\n", e->cmd);
    }
}

/* 一番上の層（知らせの文字など）を、画面の写しに重ねる */
static void blend_top(lv_draw_buf_t *screen)
{
    lv_obj_t *top = lv_layer_top();
    bool any = false;
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++)
        if (!lv_obj_is_hidden(lv_obj_get_child(top, (int32_t)i)))
            any = true;
    if (!any)
        return;
    lv_draw_buf_t *layer = lv_snapshot_take(top, LV_COLOR_FORMAT_ARGB8888);
    if (!layer)
        return;
    for (int y = 0; y < SCREEN_H; y++) {
        uint8_t *d = screen->data + (size_t)y * screen->header.stride;
        const uint8_t *s = layer->data + (size_t)y * layer->header.stride;
        for (int x = 0; x < SCREEN_W; x++, d += 4, s += 4) {
            unsigned a = s[3];
            for (int c = 0; c < 3; c++)
                d[c] = (uint8_t)((s[c] * a + d[c] * (255 - a)) / 255);
        }
    }
    lv_draw_buf_destroy(layer);
}

int recorder_run(void)
{
    ui_demo_start();
    char cmd[800];
    snprintf(cmd, sizeof cmd,
             "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %dx%d -r %d -i - "
             "-c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -movflags +faststart \"%s\"",
             SCREEN_W, SCREEN_H, fps, out_path);
    FILE *pipe = POPEN(cmd);
    if (!pipe) {
        fprintf(stderr, "record: cannot start ffmpeg\n");
        return 1;
    }

    uint32_t frames = (uint32_t)((uint64_t)end_ms * (uint32_t)fps / 1000u);
    int next = 0;
    int result = 0;
    for (uint32_t f = 0; f < frames; f++) {
        clock_ms = (uint32_t)((uint64_t)f * 1000u / (uint32_t)fps);
        while (next < event_count && events[next].ms <= clock_ms)
            run_event(&events[next++]);
        lv_timer_handler();

        lv_draw_buf_t *shot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
        if (!shot) {
            fprintf(stderr, "record: snapshot failed at frame %u\n", (unsigned)f);
            result = 1;
            break;
        }
        blend_top(shot);
        for (int y = 0; y < SCREEN_H; y++)
            fwrite(shot->data + (size_t)y * shot->header.stride, 1, SCREEN_W * 4, pipe);
        lv_draw_buf_destroy(shot);
        if (f % (uint32_t)fps == 0)
            fprintf(stderr, "\rrecord: %u / %u s", (unsigned)(f / (uint32_t)fps), (unsigned)(frames / (uint32_t)fps));
    }
    fprintf(stderr, "\n");
    if (PCLOSE(pipe) != 0) {
        fprintf(stderr, "record: ffmpeg failed\n");
        result = 1;
    }
    if (!result)
        fprintf(stderr, "record: wrote %s\n", out_path);
    return result;
}
