/*
 * SKJÁR のタッチ UI（試作）。
 *
 * 実機（fbdev）：/dev/fb0 に描き、タッチは evdev から読む。パネルは 480 × 1920 の縦長なので、
 *   LVGL の中で 90° 回して 1920 × 480 の横長として使う。
 * PC（sdl）：1920 × 480 の窓を開き、マウスでタッチの代わりをする。
 *
 * 使い方：skjar-ui [--link <tty か ->] [--rotate 0|90|180|270] [--fb <path>] [--touch <path>]
 *                  [--vt <path>] [--no-poweroff]
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if !defined(SKJAR_UI_SDL)
#include <linux/kd.h>
#include <sys/ioctl.h>
#endif

#include "lvgl.h"

#include "backlight.h"
#include "link.h"
#include "ui.h"

typedef struct {
    const char *link_path;
    int rotate;
    const char *fb_path;
    const char *touch_path;     /* NULL なら /dev/input を探す */
    const char *vt_path;        /* グラフィックモードにする仮想端末。NULL なら触らない */
    int no_poweroff;
} options_t;

static volatile sig_atomic_t quit;
static options_t opt;

static void on_signal(int sig)
{
    (void)sig;
    quit = 1;
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--link <tty|->] [--rotate 0|90|180|270] [--fb <path>] [--touch <path>]\n"
            "          [--vt <path>] [--no-poweroff]\n", argv0);
}

static int parse_args(int argc, char **argv)
{
#if defined(SKJAR_UI_SDL)
    opt.link_path = "-";
    opt.rotate = 0;
#else
    opt.link_path = "/dev/serial0";
    opt.rotate = 90;            /* TODO（実機）：パネルの向きと付け方で 90 か 270 かを決める */
    opt.fb_path = "/dev/fb0";
    opt.vt_path = "/dev/tty1";
#endif
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(a, "--link") == 0 && v) { opt.link_path = v; i++; }
        else if (strcmp(a, "--rotate") == 0 && v) { opt.rotate = atoi(v); i++; }
        else if (strcmp(a, "--fb") == 0 && v) { opt.fb_path = v; i++; }
        else if (strcmp(a, "--touch") == 0 && v) { opt.touch_path = v; i++; }
        else if (strcmp(a, "--vt") == 0 && v) { opt.vt_path = strcmp(v, "-") == 0 ? NULL : v; i++; }
        else if (strcmp(a, "--no-poweroff") == 0) { opt.no_poweroff = 1; }
        else { usage(argv[0]); return -1; }
    }
    if (opt.rotate != 0 && opt.rotate != 90 && opt.rotate != 180 && opt.rotate != 270) {
        fprintf(stderr, "--rotate must be 0, 90, 180 or 270\n");
        return -1;
    }
    return 0;
}

/* ---- 電源を切る ---- */

static void do_poweroff(void)
{
    if (opt.no_poweroff) {
        fprintf(stderr, "poweroff requested (ignored: --no-poweroff)\n");
        return;
    }
    fprintf(stderr, "poweroff\n");
    if (system("systemctl poweroff") != 0)
        fprintf(stderr, "systemctl poweroff failed\n");
}

/* ---- RP2040 とのやり取り ---- */

static void link_changed(const link_state_t *s, void *user)
{
    (void)user;
    ui_show_link_state(s);
}

static void link_shutdown(void *user)
{
    (void)user;
    fprintf(stderr, "link: shutdown requested by the RP2040\n");
    do_poweroff();
}

static void link_timer_cb(lv_timer_t *t)
{
    (void)t;
    link_poll(now_ms());
}

/* ---- 描画先 ---- */

#if defined(SKJAR_UI_SDL)

static int create_display(void)
{
    lv_display_t *disp = lv_sdl_window_create(1920, 480);
    if (!disp)
        return -1;
    lv_sdl_window_set_title(disp, "SKJAR UI");
    lv_sdl_mouse_create();
    return 0;
}

static void restore_console(void) {}

#else

static int vt_fd = -1;

/* 仮想端末をグラフィックモードにして、コンソールの文字やカーソルが UI の上に描かれないようにする */
static void take_console(void)
{
    if (!opt.vt_path)
        return;
    vt_fd = open(opt.vt_path, O_RDWR | O_NOCTTY);
    if (vt_fd < 0 || ioctl(vt_fd, KDSETMODE, KD_GRAPHICS) != 0) {
        fprintf(stderr, "cannot switch %s to graphics mode: %s\n", opt.vt_path, strerror(errno));
        if (vt_fd >= 0)
            close(vt_fd);
        vt_fd = -1;
    }
}

static void restore_console(void)
{
    if (vt_fd >= 0) {
        ioctl(vt_fd, KDSETMODE, KD_TEXT);
        close(vt_fd);
        vt_fd = -1;
    }
}

static void touch_found(lv_indev_t *indev, lv_evdev_type_t type, void *user)
{
    (void)user;
    if (type == LV_EVDEV_TYPE_ABS) {
        fprintf(stderr, "touch: found a touch device\n");
        return;
    }
    /* キーボード（HID over I2C の物理キー）やマウスは、UI では使わない */
    lv_indev_delete(indev);
}

static int create_display(void)
{
    lv_display_t *disp = lv_linux_fbdev_create();
    if (!disp)
        return -1;
    if (lv_linux_fbdev_set_file(disp, opt.fb_path) != LV_RESULT_OK) {
        fprintf(stderr, "cannot open %s\n", opt.fb_path);
        return -1;
    }
    static const lv_display_rotation_t rot[] = {
        LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270,
    };
    lv_display_set_rotation(disp, rot[opt.rotate / 90]);
    fprintf(stderr, "display: %dx%d after rotation\n",
            (int)lv_display_get_horizontal_resolution(disp), (int)lv_display_get_vertical_resolution(disp));

    /* タッチの座標は、パネルの向きのまま読む。LVGL が画面の回転に合わせて回す */
    if (opt.touch_path) {
        if (!lv_evdev_create(LV_INDEV_TYPE_POINTER, opt.touch_path))
            fprintf(stderr, "cannot open touch device %s\n", opt.touch_path);
    } else if (lv_evdev_discovery_start(touch_found, NULL) != LV_RESULT_OK) {
        fprintf(stderr, "touch device discovery failed\n");
    }
    take_console();
    return 0;
}

#endif

int main(int argc, char **argv)
{
    if (parse_args(argc, argv) != 0)
        return 2;

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    lv_init();
    lv_tick_set_cb(now_ms);

    if (create_display() != 0) {
        fprintf(stderr, "cannot create the display\n");
        return 1;
    }

    backlight_open();

    const link_callbacks_t cb = { .changed = link_changed, .shutdown = link_shutdown };
    if (!link_open(opt.link_path, &cb))
        fprintf(stderr, "running without the RP2040 link\n");
    lv_timer_create(link_timer_cb, 10, NULL);

    const ui_actions_t actions = { .poweroff = do_poweroff };
    ui_create(&actions);

    while (!quit) {
        uint32_t idle = lv_timer_handler();
        if (idle > 10)
            idle = 10;
        usleep(idle * 1000);
    }

    link_close();
    restore_console();
    return 0;
}
