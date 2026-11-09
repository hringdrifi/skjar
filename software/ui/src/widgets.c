#include "widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "link.h"
#include "movie.h"
#include "platform.h"
#include "settings.h"

#define STRIP_W        1920
#define STRIP_H        80
#define SWIPE_START    40      /* これだけ横に動いたら、ボタンではなくスワイプとみなす（px） */
#define SWIPE_MIN      160     /* 離したとき、これだけ動いていたら次のウィジェットへ（px） */
#define SLIDE_MS       220

static widgets_hooks_t hooks;
static lv_obj_t *strip;
static lv_obj_t *pages[WIDGET_COUNT];   /* ウィジェットごとの面（1920 × 80） */
static lv_obj_t *dots;                  /* いまどれを出しているかの印 */

/* 並び：いちばん左は None。その右に有効なもの */
static widget_id_t order[WIDGET_COUNT];
static int order_count;
static int current;                     /* order の何番目を出しているか */
static int32_t offset;                  /* スワイプの途中のずれ（px。左へ動かすと負） */

static const char *const names[WIDGET_COUNT] = { "None", "Pomodoro", "News" };

const char *widgets_name(widget_id_t id)
{
    return id < WIDGET_COUNT ? names[id] : "";
}

static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}

/* 帯の中のボタン。押した・離したは widgets_input で決めるので、LVGL のクリックは切る */
#define MAX_BUTTONS 8
typedef struct {
    lv_obj_t *obj;
    lv_obj_t *text;
    void (*cb)(void);
} wbutton_t;
static wbutton_t buttons[MAX_BUTTONS];
static int button_count;

static wbutton_t *button(lv_obj_t *parent, const char *text, int width, void (*cb)(void))
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, width, 64);
    lv_obj_set_clickable(b, false);
    if (hooks.style_button)
        hooks.style_button(b);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_28, 0);
    lv_obj_center(l);
    wbutton_t *w = &buttons[button_count < MAX_BUTTONS ? button_count++ : MAX_BUTTONS - 1];
    *w = (wbutton_t){ b, l, cb };
    return w;
}

/* ---- Pomodoro ---- */

#define POMO_WORK_MS    (25u * 60u * 1000u)
#define POMO_SHORT_MS   (5u * 60u * 1000u)
#define POMO_LONG_MS    (15u * 60u * 1000u)
#define POMO_ROUNDS     4           /* 集中を 4 回したら長い休憩 */

typedef enum { POMO_WORK, POMO_SHORT, POMO_LONG } pomo_phase_t;

static struct {
    pomo_phase_t phase;
    int round;                  /* いまの集中が何回目か（1〜POMO_ROUNDS） */
    bool running;
    uint32_t remaining_ms;      /* 止めているときの残り */
    uint32_t started_ms;        /* 動かし始めた時刻（running のとき） */
    uint32_t started_remaining; /* 動かし始めたときの残り */
    lv_obj_t *phase_label, *time_label, *bar;
    wbutton_t *start;
} pomo;

static uint32_t pomo_length(pomo_phase_t p)
{
    return p == POMO_WORK ? POMO_WORK_MS : p == POMO_SHORT ? POMO_SHORT_MS : POMO_LONG_MS;
}

static uint32_t pomo_remaining(void)
{
    if (!pomo.running)
        return pomo.remaining_ms;
    uint32_t used = lv_tick_elaps(pomo.started_ms);
    return used >= pomo.started_remaining ? 0 : pomo.started_remaining - used;
}

static void pomo_show(void)
{
    uint32_t left = pomo_remaining();
    uint32_t secs = (left + 999) / 1000;
    lv_label_set_text_fmt(pomo.time_label, "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));

    lv_color_t color;
    if (pomo.phase == POMO_WORK) {
        lv_label_set_text_fmt(pomo.phase_label, "Focus  %d/%d", pomo.round, POMO_ROUNDS);
        color = lv_palette_main(LV_PALETTE_RED);
    } else {
        lv_label_set_text(pomo.phase_label, pomo.phase == POMO_SHORT ? "Short break" : "Long break");
        color = lv_palette_main(LV_PALETTE_GREEN);
    }
    lv_obj_set_style_text_color(pomo.phase_label, color, 0);
    lv_obj_set_style_bg_color(pomo.bar, color, LV_PART_INDICATOR);
    uint32_t total = pomo_length(pomo.phase);
    lv_bar_set_value(pomo.bar, (int32_t)((uint64_t)(total - left) * 1000 / total), LV_ANIM_OFF);
    lv_label_set_text(pomo.start->text, pomo.running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    /* 止めているときは時間を薄く */
    lv_obj_set_style_text_opa(pomo.time_label, pomo.running ? LV_OPA_COVER : LV_OPA_60, 0);
}

static void pomo_set_phase(pomo_phase_t p)
{
    pomo.phase = p;
    pomo.running = false;
    pomo.remaining_ms = pomo_length(p);
}

/* キーのライトを数回点滅させて知らせる（LEDF。0.3 秒届かなければ RP2040 が消す） */
#define FLASH_MS        1600
#define FLASH_PERIOD_MS 400
#define FLASH_LEVEL     10
static uint32_t flash_started;
static lv_timer_t *flash_timer;

static void flash_cb(lv_timer_t *t)
{
    uint32_t el = lv_tick_elaps(flash_started);
    if (el >= FLASH_MS || movie_playing()) {
        lv_timer_delete(t);
        flash_timer = NULL;
        return;
    }
    uint8_t levels[42];
    memset(levels, (el % FLASH_PERIOD_MS) < FLASH_PERIOD_MS / 2 ? FLASH_LEVEL : 0, sizeof levels);
    link_send_leds(levels);
}

static void flash_keys(void)
{
    flash_started = lv_tick_get();
    if (!flash_timer)
        flash_timer = lv_timer_create(flash_cb, 50, NULL);
}

/* 1 つの区切りが終わった：次へ進め、止めて待つ（次は ▶ で始める）。announce なら、キーのライトと文字で知らせる */
static void pomo_finished(bool announce)
{
    const char *msg;
    if (pomo.phase == POMO_WORK) {
        if (pomo.round >= POMO_ROUNDS) {
            pomo_set_phase(POMO_LONG);
            msg = "Pomodoro: focus done. Take a long break";
        } else {
            pomo_set_phase(POMO_SHORT);
            msg = "Pomodoro: focus done. Take a short break";
        }
    } else {
        pomo.round = pomo.phase == POMO_LONG ? 1 : pomo.round + 1;
        pomo_set_phase(POMO_WORK);
        msg = "Pomodoro: break is over. Back to focus";
    }
    if (!announce)
        return;
    flash_keys();
    if (hooks.notify)
        hooks.notify(msg);
}

static void pomo_tick(lv_timer_t *t)
{
    (void)t;
    if (pomo.running && pomo_remaining() == 0)
        pomo_finished(true);
    pomo_show();
}

static void pomo_start_pause(void)
{
    if (pomo.running) {
        pomo.remaining_ms = pomo_remaining();
        pomo.running = false;
    } else {
        pomo.started_remaining = pomo.remaining_ms;
        pomo.started_ms = lv_tick_get();
        pomo.running = true;
    }
    pomo_show();
}

/* 次の区切りへ飛ばす（知らせない） */
static void pomo_skip(void)
{
    pomo_finished(false);
    pomo_show();
}

static void pomo_reset(void)
{
    pomo.round = 1;
    pomo_set_phase(POMO_WORK);
    pomo_show();
}

static void create_pomodoro(lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(page, 24, 0);
    lv_obj_set_style_pad_column(page, 28, 0);

    pomo.phase_label = label(page, &lv_font_montserrat_28, lv_color_white());
    lv_obj_set_width(pomo.phase_label, 240);
    pomo.time_label = label(page, &lv_font_montserrat_40, lv_color_white());
    lv_obj_set_width(pomo.time_label, 130);

    pomo.bar = lv_bar_create(page);
    lv_obj_set_clickable(pomo.bar, false);
    lv_obj_set_height(pomo.bar, 14);
    lv_obj_set_flex_grow(pomo.bar, 1);
    lv_bar_set_range(pomo.bar, 0, 1000);
    lv_obj_set_style_bg_color(pomo.bar, lv_color_hex(0x3A3F47), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(pomo.bar, LV_OPA_COVER, LV_PART_MAIN);

    pomo.start = button(page, LV_SYMBOL_PLAY, 120, pomo_start_pause);
    button(page, LV_SYMBOL_NEXT, 120, pomo_skip);
    button(page, LV_SYMBOL_REFRESH, 120, pomo_reset);

    pomo_reset();
    lv_timer_create(pomo_tick, 250, NULL);
}

/* ---- News：電光掲示板 ---- */

#define NEWS_MAX        6000        /* 読む見出しの合計（バイト） */
#define NEWS_CHECK_MS   30000       /* news.txt が変わったかを見る間隔 */
#define NEWS_SPEED      160         /* 流れる速さ（px / 秒） */
#define NEWS_SEP        "      |      "

static struct {
    lv_obj_t *ticker, *stamp;
    char path[600];
    time_t mtime;
    bool loaded;
} news;

static void news_set(const char *text, time_t mtime)
{
    lv_label_set_text(news.ticker, text);
    if (mtime) {
        struct tm tm;
        platform_localtime(mtime, &tm);
        lv_label_set_text_fmt(news.stamp, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        lv_label_set_text(news.stamp, "--:--");
    }
}

/* news.txt を読んで 1 本の文字列にする。字形のない文字（ASCII 以外）は飛ばす（フォントが英数字だけのため） */
static void news_load(void)
{
    struct stat st;
    if (stat(news.path, &st) != 0) {
        if (news.loaded || news.mtime != (time_t)-1) {
            news.mtime = (time_t)-1;
            news.loaded = false;
            news_set("No news yet.  Run  tools/fetch_news.py  to fetch the headlines (news.txt next to ui.conf)", 0);
        }
        return;
    }
    if (news.loaded && st.st_mtime == news.mtime)
        return;
    FILE *f = fopen(news.path, "rb");
    if (!f)
        return;
    static char text[NEWS_MAX + 64];
    size_t n = 0;
    char line[1024];
    while (fgets(line, sizeof line, f) && n < NEWS_MAX) {
        line[strcspn(line, "\r\n")] = '\0';
        char clean[1024];
        size_t c = 0;
        for (const unsigned char *p = (const unsigned char *)line; *p; p++)
            if (*p >= 0x20 && *p < 0x7F)
                clean[c++] = (char)*p;
        clean[c] = '\0';
        if (c == 0)
            continue;
        int w = snprintf(text + n, sizeof text - n, "%s%s", n ? NEWS_SEP : "", clean);
        if (w < 0)
            break;
        n += (size_t)w;
        if (n >= sizeof text)
            n = sizeof text - 1;
    }
    fclose(f);
    text[n] = '\0';
    news.mtime = st.st_mtime;
    news.loaded = true;
    news_set(n ? text : "news.txt is empty", st.st_mtime);
}

static void news_check(lv_timer_t *t)
{
    (void)t;
    news_load();
}

static void create_news(lv_obj_t *page)
{
    snprintf(news.path, sizeof news.path, "%s/news.txt", settings_dir());
    news.mtime = 0;

    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(page, 16, 0);
    lv_obj_set_style_pad_column(page, 20, 0);

    const lv_color_t amber = lv_palette_main(LV_PALETTE_AMBER);

    /* 左の札：NEWS と、見出しを取ってきた時刻 */
    lv_obj_t *badge = box(page);
    lv_obj_set_size(badge, 120, 64);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, amber, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = label(badge, &lv_font_montserrat_28, lv_color_black());
    lv_label_set_text(title, "NEWS");
    news.stamp = label(badge, &lv_font_montserrat_14, lv_color_black());

    /* 掲示板：黒地に琥珀色の文字を、右から左へ流し続ける */
    lv_obj_t *board = box(page);
    lv_obj_set_height(board, 64);
    lv_obj_set_flex_grow(board, 1);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x0B0B0B), 0);
    lv_obj_set_style_clip_corner(board, true, 0);
    lv_obj_set_style_pad_hor(board, 12, 0);
    news.ticker = label(board, &lv_font_montserrat_40, amber);
    lv_obj_set_width(news.ticker, LV_PCT(100));
    lv_label_set_long_mode(news.ticker, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(news.ticker, lv_anim_speed(NEWS_SPEED), 0);
    lv_obj_align(news.ticker, LV_ALIGN_LEFT_MID, 0, 0);

    news_load();
    lv_timer_create(news_check, NEWS_CHECK_MS, NULL);
}

/* ---- 並びと、スワイプでの切り替え ---- */

static void layout(void)
{
    /* いまのウィジェットを offset の位置に、隣をその左右に置く。ほかは隠す */
    for (int i = 0; i < WIDGET_COUNT; i++)
        lv_obj_set_hidden(pages[i], true);
    for (int k = current - 1; k <= current + 1; k++) {
        if (k < 0 || k >= order_count)
            continue;
        lv_obj_t *p = pages[order[k]];
        lv_obj_set_hidden(p, false);
        lv_obj_set_x(p, offset + (k - current) * STRIP_W);
    }
}

static void update_dots(void)
{
    lv_obj_clean(dots);
    lv_obj_set_hidden(dots, order_count < 2);
    for (int k = 0; k < order_count; k++) {
        lv_obj_t *d = box(dots);
        lv_obj_set_size(d, k == current ? 18 : 6, 6);
        lv_obj_set_style_radius(d, 3, 0);
        lv_obj_set_style_bg_opa(d, k == current ? LV_OPA_80 : LV_OPA_40, 0);
        lv_obj_set_style_bg_color(d, lv_color_white(), 0);
    }
}

static void slide_exec(void *var, int32_t v)
{
    (void)var;
    offset = v;
    layout();
}

static void slide_to(int target)
{
    if (target < 0)
        target = 0;
    if (target >= order_count)
        target = order_count - 1;
    /* 次のウィジェットへ：いまの位置（offset）から、見た目がつながるように滑らせる */
    offset += (target - current) * STRIP_W;
    current = target;
    update_dots();
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, strip);
    lv_anim_set_exec_cb(&a, slide_exec);
    lv_anim_set_values(&a, offset, 0);
    lv_anim_set_duration(&a, SLIDE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void widgets_refresh(void)
{
    widget_id_t shown = order_count ? order[current] : WIDGET_NONE;
    order_count = 0;
    order[order_count++] = WIDGET_NONE;
    for (int i = WIDGET_NONE + 1; i < WIDGET_COUNT; i++)
        if (settings()->widgets[i])
            order[order_count++] = (widget_id_t)i;
    /* 出していたものが残っていればそのまま、外したなら None に */
    current = 0;
    for (int k = 0; k < order_count; k++)
        if (order[k] == shown)
            current = k;
    lv_anim_delete(strip, slide_exec);
    offset = 0;
    layout();
    update_dots();
}

void widgets_create(lv_obj_t *parent, const widgets_hooks_t *h)
{
    if (h)
        hooks = *h;
    strip = parent;
    lv_obj_set_scrollable(strip, false);
    lv_obj_set_style_clip_corner(strip, true, 0);
    for (int i = 0; i < WIDGET_COUNT; i++) {
        pages[i] = box(strip);
        lv_obj_set_size(pages[i], STRIP_W, STRIP_H);
    }
    create_pomodoro(pages[WIDGET_POMODORO]);
    create_news(pages[WIDGET_NEWS]);

    dots = box(strip);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_set_ignore_layout(dots, true);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -2);

    widgets_refresh();
}

/* ---- 指 ---- */

static struct {
    bool down;
    bool swiping;
    lv_point_t start, last;
    wbutton_t *pressed;
    int32_t base;               /* 触れたときの offset（滑っている途中で触れたとき） */
} touch;

static bool inside(lv_obj_t *obj, const lv_point_t *p)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    return p->x >= a.x1 && p->x <= a.x2 && p->y >= a.y1 && p->y <= a.y2;
}

static wbutton_t *button_at(const lv_point_t *p)
{
    for (int i = 0; i < button_count; i++) {
        lv_obj_t *b = buttons[i].obj;
        /* 出しているウィジェットのボタンだけ */
        if (lv_obj_get_parent(b) == pages[order[current]] && inside(b, p))
            return &buttons[i];
    }
    return NULL;
}

static void set_pressed(wbutton_t *b)
{
    if (touch.pressed)
        lv_obj_remove_state(touch.pressed->obj, LV_STATE_PRESSED);
    touch.pressed = b;
    if (b)
        lv_obj_add_state(b->obj, LV_STATE_PRESSED);
}

void widgets_release(void)
{
    set_pressed(NULL);
    if (touch.swiping)
        slide_to(current);
    touch.down = touch.swiping = false;
}

void widgets_input(const touch_contacts_t *c)
{
    /* 帯の中の指を 1 本だけ見る（触れ始めた指を追う代わりに、帯の中で最初に見つかった指） */
    const lv_point_t *p = NULL;
    for (int k = 0; k < c->count; k++) {
        if (touch.down || inside(strip, &c->points[k])) {
            p = &c->points[k];
            break;
        }
    }

    if (p) {
        if (!touch.down) {
            touch.down = true;
            touch.swiping = false;
            touch.start = *p;
            lv_anim_delete(strip, slide_exec);
            touch.base = offset;
            set_pressed(button_at(p));
        }
        touch.last = *p;
        int32_t dx = p->x - touch.start.x;
        if (!touch.swiping && order_count > 1 && LV_ABS(dx) > SWIPE_START) {
            touch.swiping = true;
            set_pressed(NULL);
        }
        if (touch.swiping) {
            /* 端（None の左、いちばん右の右）では、引っぱった分の 1/3 しか動かさない */
            int32_t o = touch.base + dx;
            if ((current == 0 && o > 0) || (current == order_count - 1 && o < 0))
                o /= 3;
            offset = o;
            layout();
        } else if (touch.pressed && !inside(touch.pressed->obj, p)) {
            set_pressed(NULL);  /* ボタンの外へ出たら、押していないことにする */
        }
        return;
    }

    if (!touch.down)
        return;
    /* 離した */
    touch.down = false;
    if (touch.swiping) {
        touch.swiping = false;
        int32_t dx = touch.last.x - touch.start.x;
        int target = current;
        if (dx <= -SWIPE_MIN)
            target = current + 1;   /* 左へはらう：右のウィジェットへ */
        else if (dx >= SWIPE_MIN)
            target = current - 1;
        slide_to(target);
        return;
    }
    wbutton_t *b = touch.pressed;
    set_pressed(NULL);
    if (b && b->cb)
        b->cb();
}
