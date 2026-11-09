#include "touch_source.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static lv_display_t *display;

/* ---- LVGL のポインタを使う（マルチタッチがないとき） ---- */

static void read_from_lvgl_pointer(touch_contacts_t *out)
{
    out->count = 0;
    lv_indev_t *indev = NULL;
    while ((indev = lv_indev_get_next(indev)) != NULL) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
            break;
    }
    if (!indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED)
        return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int fingers = 1;
#if defined(_WIN32)
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        fingers = 3;
    else if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        fingers = 2;
#endif
    for (int i = 0; i < fingers; i++)
        out->points[i] = p;
    out->count = fingers;
}

#if defined(SKJAR_UI_FBDEV)

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define BIT_SET(bits, n) ((bits)[(n) / (8 * sizeof(long))] & (1UL << ((n) % (8 * sizeof(long)))))

typedef struct {
    bool active;
    int x, y;           /* 生の値 */
} slot_t;

static int mt_fd = -1;
static slot_t slots[TOUCH_MAX_CONTACTS];
static int cur_slot;
static struct input_absinfo abs_x, abs_y;

static bool is_multitouch(int fd)
{
    unsigned long abs_bits[(ABS_MAX + 1) / (8 * sizeof(long)) + 1];
    memset(abs_bits, 0, sizeof abs_bits);
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof abs_bits), abs_bits) < 0)
        return false;
    return BIT_SET(abs_bits, ABS_MT_SLOT) && BIT_SET(abs_bits, ABS_MT_POSITION_X) && BIT_SET(abs_bits, ABS_MT_POSITION_Y);
}

static int open_multitouch(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    if (!is_multitouch(fd)
        || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &abs_x) < 0
        || ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &abs_y) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int find_multitouch(void)
{
    DIR *d = opendir("/dev/input");
    if (!d)
        return -1;
    struct dirent *e;
    int fd = -1;
    while (fd < 0 && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0)
            continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        fd = open_multitouch(path);
        if (fd >= 0)
            fprintf(stderr, "touch: multi-touch device %s\n", path);
    }
    closedir(d);
    return fd;
}

static int map(int v, const struct input_absinfo *a, int size)
{
    if (a->maximum <= a->minimum)
        return v;
    int p = (int)((long long)(v - a->minimum) * (size - 1) / (a->maximum - a->minimum));
    return p < 0 ? 0 : p >= size ? size - 1 : p;
}

static void drain_events(void)
{
    struct input_event ev;
    while (read(mt_fd, &ev, sizeof ev) == (ssize_t)sizeof ev) {
        if (ev.type != EV_ABS)
            continue;
        switch (ev.code) {
        case ABS_MT_SLOT:
            cur_slot = ev.value;
            break;
        case ABS_MT_TRACKING_ID:
            if (cur_slot >= 0 && cur_slot < TOUCH_MAX_CONTACTS)
                slots[cur_slot].active = ev.value >= 0;
            break;
        case ABS_MT_POSITION_X:
            if (cur_slot >= 0 && cur_slot < TOUCH_MAX_CONTACTS)
                slots[cur_slot].x = ev.value;
            break;
        case ABS_MT_POSITION_Y:
            if (cur_slot >= 0 && cur_slot < TOUCH_MAX_CONTACTS)
                slots[cur_slot].y = ev.value;
            break;
        default:
            break;
        }
    }
}

void touch_source_open(lv_display_t *disp, const char *path)
{
    display = disp;
    mt_fd = path ? open_multitouch(path) : find_multitouch();
    if (mt_fd < 0)
        fprintf(stderr, "touch: no multi-touch device, the trackpad uses one finger\n");
}

void touch_source_read(touch_contacts_t *out)
{
    if (mt_fd < 0) {
        read_from_lvgl_pointer(out);
        return;
    }
    drain_events();
    /* パネルの向き（回す前）の大きさに合わせてから、LVGL と同じく画面の回転に合わせて回す */
    int w = (int)lv_display_get_original_horizontal_resolution(display);
    int h = (int)lv_display_get_original_vertical_resolution(display);
    out->count = 0;
    for (int i = 0; i < TOUCH_MAX_CONTACTS; i++) {
        if (!slots[i].active)
            continue;
        lv_point_t p = { map(slots[i].x, &abs_x, w), map(slots[i].y, &abs_y, h) };
        lv_display_rotate_point(display, &p);
        out->points[out->count++] = p;
    }
}

#else

void touch_source_open(lv_display_t *disp, const char *path)
{
    (void)path;
    display = disp;
}

void touch_source_read(touch_contacts_t *out)
{
    read_from_lvgl_pointer(out);
}

#endif
