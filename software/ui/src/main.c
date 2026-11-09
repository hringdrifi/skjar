/*
 * SKJÁR のタッチ UI（試作）。
 *
 * 実機（fbdev）：/dev/fb0 に描き、タッチは evdev から読む。パネルは 480 × 1920 の縦長なので、
 *   LVGL の中で 90° 回して 1920 × 480 の横長として使う。
 * PC（sdl）：Linux / WSL で 1920 × 480 の窓を開き、マウスでタッチの代わりをする。
 * PC（windows）：Windows で同じ窓を開く（Win32）。
 *
 * 使い方：skjar-ui [--link <tty か COMn か ->] [--rotate 0|90|180|270] [--fb <path>]
 *                  [--touch <path>] [--vt <path>] [--zoom <percent>] [--config <path>] [--background <png>] [--movie <skm>]
 *                  [--start boot|manual] [--no-poweroff]
 *
 * キーボードモードとサイバーデックモード：このタッチ UI を出している間がキーボードモード（物理キーは PC へ）。
 * System タブの Console で UI を閉じるとサイバーデックモード（コンソール。物理キーは CM0 へ）。コンソールで
 * skjar と打つと UI が立ち上がり、キーボードモードに戻る。どちらのモードかは RP2040 に OUT で知らせ、
 * RP2040 はモードごとのキーマップに切り替える。
 *   --start boot   ：電源を入れたとき。PC につながっていなければ、UI を出さずにコンソールへ
 *   --start manual ：コンソールで skjar と打ったとき（初期値）。PC がなければ「PC につないで」を出す
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SKJAR_UI_FBDEV)
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "lvgl.h"

#include "backlight.h"
#include "link.h"
#include "platform.h"
#include "settings.h"
#include "ui.h"

typedef struct {
    const char *link_path;
    int rotate;
    const char *fb_path;
    const char *touch_path;     /* NULL なら /dev/input を探す */
    const char *vt_path;        /* グラフィックモードにする仮想端末。NULL なら触らない */
    int zoom;                   /* 窓の大きさ（%）。PC の窓だけ */
    const char *config_path;    /* 設定のファイル。NULL なら settings.h の場所 */
    const char *background;     /* 背景の画像（PNG）。NULL なら描いた絵 */
    const char *movie;          /* 隠し機能の動画（.skm）。NULL なら設定のフォルダの movie.skm */
    int boot;                   /* --start boot：電源を入れたときの起動 */
    int no_poweroff;
} options_t;

static volatile sig_atomic_t quit;
static bool to_console;          /* コンソールへ移るために終わる */
static options_t opt;

static void on_signal(int sig)
{
    (void)sig;
    quit = 1;
}

static uint32_t now_ms(void)
{
    return platform_now_ms();
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--link <tty|COMn|->] [--rotate 0|90|180|270] [--fb <path>] [--touch <path>]\n"
            "          [--vt <path>] [--zoom <percent>] [--config <path>] [--background <png>] [--movie <skm>]\n"
            "          [--start boot|manual] [--no-poweroff]\n", argv0);
}

static int parse_args(int argc, char **argv)
{
#if defined(SKJAR_UI_FBDEV)
    opt.link_path = "/dev/serial0";
    opt.rotate = 90;            /* TODO（実機）：パネルの向きと付け方で 90 か 270 かを決める */
    opt.fb_path = "/dev/fb0";
    opt.vt_path = "/dev/tty1";
#else
    opt.link_path = "-";
    opt.rotate = 0;
    opt.no_poweroff = 1;        /* PC で試すときに PC の電源を切らない */
#endif
    opt.zoom = 100;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (strcmp(a, "--link") == 0 && v) { opt.link_path = v; i++; }
        else if (strcmp(a, "--rotate") == 0 && v) { opt.rotate = atoi(v); i++; }
        else if (strcmp(a, "--fb") == 0 && v) { opt.fb_path = v; i++; }
        else if (strcmp(a, "--touch") == 0 && v) { opt.touch_path = v; i++; }
        else if (strcmp(a, "--vt") == 0 && v) { opt.vt_path = strcmp(v, "-") == 0 ? NULL : v; i++; }
        else if (strcmp(a, "--zoom") == 0 && v) { opt.zoom = atoi(v); i++; }
        else if (strcmp(a, "--config") == 0 && v) { opt.config_path = v; i++; }
        else if (strcmp(a, "--background") == 0 && v) { opt.background = v; i++; }
        else if (strcmp(a, "--movie") == 0 && v) { opt.movie = v; i++; }
        else if (strcmp(a, "--start") == 0 && v) { opt.boot = strcmp(v, "boot") == 0; i++; }
        else if (strcmp(a, "--no-poweroff") == 0) { opt.no_poweroff = 1; }
        else { usage(argv[0]); return -1; }
    }
    if (opt.rotate != 0 && opt.rotate != 90 && opt.rotate != 180 && opt.rotate != 270) {
        fprintf(stderr, "--rotate must be 0, 90, 180 or 270\n");
        return -1;
    }
    if (opt.zoom < 25 || opt.zoom > 400) {
        fprintf(stderr, "--zoom must be between 25 and 400\n");
        return -1;
    }
    return 0;
}

/* ---- 電源を切る ---- */

/* サイバーデックモードへ：RP2040 に知らせて（物理キーを CM0 へ、サイバーデックのキーマップへ）、UI を閉じる。
 * UI が終わると、systemd が tty1 のログイン（コンソール）を立ち上げる（skjar-ui@.service） */
static void open_console(void)
{
    fprintf(stderr, "switching to the console (cyberdeck mode)\n");
    link_send_out(LINK_OUT_CM0);
    to_console = true;
    quit = 1;
}

/* --start boot：電源を入れたとき。RP2040 から PC につながっているかが届くのを 3 秒まで待ち、
 * つながっていれば UI のまま（キーボードモード）、そうでなければコンソールへ */
#define BOOT_WAIT_MS 3000

static void boot_check_cb(lv_timer_t *t)
{
    static uint32_t started;
    if (!started)
        started = lv_tick_get();
    const link_state_t *s = link_state();
    if (s->connected && s->pc_connected == 1) {
        lv_timer_delete(t);             /* PC がある：キーボードモードのまま */
        return;
    }
    if ((s->connected && s->pc_connected == 0) || lv_tick_elaps(started) >= BOOT_WAIT_MS) {
        lv_timer_delete(t);
        open_console();
    }
}

static void do_poweroff(void)
{
    if (opt.no_poweroff) {
        fprintf(stderr, "poweroff requested (ignored)\n");
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

static void link_touch_label(int layer, int index, const char *code, void *user)
{
    (void)user;
    ui_set_touch_label(layer, index, code);
}

static void link_user_key(int id, bool pressed, void *user)
{
    (void)user;
    ui_user_key(id, pressed);
}

static void link_activity(void *user)
{
    (void)user;
    ui_activity();
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
    lv_tick_set_cb(now_ms);
    lv_display_t *disp = lv_sdl_window_create(1920, 480);
    if (!disp)
        return -1;
    lv_sdl_window_set_title(disp, "SKJAR UI");
    if (opt.zoom != 100)
        lv_sdl_window_set_zoom(disp, opt.zoom / 100.0f);
    lv_sdl_mouse_create();
    return 0;
}

static void restore_console(void) {}

#elif defined(SKJAR_UI_WINDOWS)

static int create_display(void)
{
    /* 時刻の関数は、LVGL の Windows のドライバが登録する。
     * 画面の拡大率（DPI）には従わず、--zoom の大きさにする */
    lv_display_t *disp = lv_windows_create_display(L"SKJAR UI", 1920, 480, opt.zoom, true, true);
    if (!disp)
        return -1;
    lv_windows_acquire_pointer_indev(disp);
    return 0;
}

static void restore_console(void) {}

#else /* fbdev */

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
    lv_tick_set_cb(now_ms);
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

    if (create_display() != 0) {
        fprintf(stderr, "cannot create the display\n");
        return 1;
    }

    backlight_open();

    settings_load(opt.config_path);

    const link_callbacks_t cb = {
        .changed = link_changed,
        .shutdown = link_shutdown,
        .touch_label = link_touch_label,
        .user_key = link_user_key,
        .activity = link_activity,
    };
    if (!link_open(opt.link_path, &cb))
        fprintf(stderr, "running without the RP2040 link\n");
    lv_timer_create(link_timer_cb, 10, NULL);

    /* 隠し機能の動画：渡されなければ、設定のフォルダの movie.skm（音は movie.wav） */
    static char movie_path[600];
    if (opt.movie)
        snprintf(movie_path, sizeof movie_path, "%s", opt.movie);
    else
#if defined(_WIN32)
        snprintf(movie_path, sizeof movie_path, "%s\\movie.skm", settings_dir());
#else
        snprintf(movie_path, sizeof movie_path, "%s/movie.skm", settings_dir());
#endif

    const ui_actions_t actions = {
        .poweroff = do_poweroff,
        .background_png = opt.background,
        .movie_path = movie_path,
        .open_console = open_console,
    };
    ui_create(&actions);

    /* キーボードモードで始まる（物理キーは PC へ、キーボードモードのキーマップへ） */
    link_send_out(LINK_OUT_PC);
    if (opt.boot)
        lv_timer_create(boot_check_cb, 100, NULL);

    /* 窓を閉じると表示がなくなる（Windows のドライバ） */
    while (!quit && lv_display_get_default() != NULL) {
        uint32_t idle = lv_timer_handler();
        if (idle > 10)
            idle = 10;
        platform_sleep_ms(idle);
    }

    if (to_console)
        fprintf(stderr, "closed for the console\n");
    link_close();
    restore_console();
    return 0;
}
