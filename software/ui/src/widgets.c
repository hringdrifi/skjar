#include "widgets.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>

#include "icons.h"
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

static const char *const names[WIDGET_COUNT] = { "None", "Typing", "Pomodoro", "Weather", "News" };

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

/* ボタンの形をしていない、叩ける場所（天気の「いま」など）。押している間だけうっすら明るくする */
static void zone(lv_obj_t *obj, void (*cb)(void))
{
    lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_10, LV_STATE_PRESSED);
    wbutton_t *w = &buttons[button_count < MAX_BUTTONS ? button_count++ : MAX_BUTTONS - 1];
    *w = (wbutton_t){ obj, NULL, cb };
}

/* ---- 選ぶ画面（Weather の場所、News のジャンル） ----
 * 画面全体を暗くして、真ん中の板に一覧を出す（lv_layer_top。開いているときだけ作る）。
 * 帯のボタンと違い、LVGL のクリックで操作する（開いている間、ui.c は指をタッチキーに渡さない） */

static lv_obj_t *picker;

static void picker_close(void)
{
    if (picker) {
        lv_obj_delete(picker);
        picker = NULL;
    }
}

static void picker_close_cb(lv_event_t *e)
{
    /* 暗くした背景を叩いたら閉じる（中の板を叩いたときは閉じない） */
    if (lv_event_get_target(e) == lv_event_get_current_target(e))
        picker_close();
}

static void picker_cancel_cb(lv_event_t *e)
{
    (void)e;
    picker_close();
}

static lv_obj_t *picker_button(lv_obj_t *panel, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(panel);
    lv_obj_set_size(b, 200, 64);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (hooks.style_button)
        hooks.style_button(b);
    lv_obj_t *l = label(b, &lv_font_montserrat_28, lv_color_white());
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

/* 題と Cancel と、選ぶもののボタンの並び（map。離したときに選ぶ、押すと印が付く）を作り、並びを返す。
 * 板（並びの親）の右上の Cancel の左は空けてある（News の Done） */
static lv_obj_t *picker_create(const char *title_text, const char *const *map, int32_t matrix_h)
{
    lv_obj_t *dim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dim);
    lv_obj_set_size(dim, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dim, LV_OPA_70, 0);
    lv_obj_add_event_cb(dim, picker_close_cb, LV_EVENT_CLICKED, NULL);
    picker = dim;

    lv_obj_t *panel = lv_obj_create(dim);
    lv_obj_set_size(panel, 1640, matrix_h + 120);
    lv_obj_center(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_style_pad_all(panel, 20, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1E24), 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 16, 0);

    lv_obj_t *title = label(panel, &lv_font_montserrat_28, lv_color_white());
    lv_label_set_text(title, title_text);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 14);
    lv_obj_align(picker_button(panel, "Cancel", picker_cancel_cb), LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *m = lv_buttonmatrix_create(panel);
    lv_buttonmatrix_set_map(m, map);
    lv_obj_set_size(m, LV_PCT(100), matrix_h);
    lv_obj_align(m, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_row(m, 12, 0);
    lv_obj_set_style_pad_column(m, 12, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_text_font(m, &lv_font_montserrat_28, 0);
    /* 離したときに選ぶ（押したときだと、指を滑らせて選び直せない） */
    lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_CHECKABLE);
    return m;
}

bool widgets_picker_open(void)
{
    return picker != NULL;
}

/* 選び直したので、取ってくる道具をすぐ動かす。設定のフォルダの <request> に中身を書く（実機では systemd の
 * path ユニットがそれを見て動かす）。Windows では systemd がないので、自分で tools/<tool> を動かす。
 * 録画のときは何もしないで false */
static bool refetch(const char *request, const char *content, const char *tool, const char *args)
{
    if (settings_is_readonly())
        return false;
    char path[600];
    snprintf(path, sizeof path, "%s/%s", settings_dir(), request);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "%s\n", content);
        fclose(f);
    }
    platform_run_tool(tool, args);
    return true;
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
    lv_obj_t *ticker, *stamp, *title, *badge, *board;
    char path[600];
    time_t mtime;
    bool loaded;
    bool unicode;               /* 日本語のフォントがある：ASCII 以外もそのまま出す */
    int fast_checks;            /* ジャンルを選んだあと、あと何回 1 秒ごとに見るか */
} news;

/* ---- 日本語のフォント ---- */

#define NEWS_FONT_PX    40
#define FONT_MAX_BYTES  (40u * 1024u * 1024u)

static const char *const font_candidates[] = {
#if defined(_WIN32)
    "C:\\Windows\\Fonts\\BIZ-UDGothicR.ttc",
    "C:\\Windows\\Fonts\\YuGothM.ttc",
    "C:\\Windows\\Fonts\\meiryo.ttc",
#else
    "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf",        /* apt install fonts-ipafont-gothic */
    "/usr/share/fonts/truetype/fonts-japanese-gothic.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",    /* apt install fonts-noto-cjk */
#endif
    NULL
};

/* ファイルを丸ごと読んで、Tiny TTF のフォントにする。読んだ中身はフォントが使い続けるので解放しない */
static lv_font_t *font_from_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    lv_font_t *font = NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && (unsigned long)size <= FONT_MAX_BYTES) {
        void *data = malloc((size_t)size);
        if (data && fread(data, 1, (size_t)size, f) == (size_t)size)
            font = lv_tiny_ttf_create_data(data, (size_t)size, NEWS_FONT_PX);
        if (!font)
            free(data);
    }
    fclose(f);
    return font;
}

static lv_font_t *load_japanese_font(const char *path)
{
    if (path) {
        lv_font_t *font = font_from_file(path);
        fprintf(stderr, "news: font %s %s\n", path, font ? "loaded" : "could not be read");
        return font;
    }
    for (int i = 0; font_candidates[i]; i++) {
        lv_font_t *font = font_from_file(font_candidates[i]);
        if (font) {
            fprintf(stderr, "news: font %s\n", font_candidates[i]);
            return font;
        }
    }
    fprintf(stderr, "news: no Japanese font found, showing ASCII headlines only\n");
    return NULL;
}

static void news_set(const char *text, time_t mtime)
{
    /* 1 周の時間は、文字の幅から自分で決める。LVGL の速さ指定（lv_anim_speed）は 1 周を 10 秒までに
     * 抑えるので、見出しを何本もつなぐと、決めた速さよりずっと速く流れてしまう */
    lv_point_t size;
    lv_text_get_size(&size, text, lv_obj_get_style_text_font(news.ticker, LV_PART_MAIN), 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    uint32_t ms = (uint32_t)((uint64_t)size.x * 1000 / NEWS_SPEED);
    lv_obj_set_style_anim_duration(news.ticker, ms ? ms : 1, 0);
    lv_label_set_text(news.ticker, text);
    if (mtime) {
        struct tm tm;
        platform_localtime(mtime, &tm);
        lv_label_set_text_fmt(news.stamp, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        lv_label_set_text(news.stamp, "--:--");
    }
}

/* news.txt（UTF-8）を読んで 1 本の文字列にする。日本語のフォントがなければ、ASCII 以外を含む見出しは
 * 飛ばす（Montserrat に字形がなく、一部の字が抜けた見出しになるため） */
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
    int skipped = 0;
    while (fgets(line, sizeof line, f) && n < NEWS_MAX) {
        line[strcspn(line, "\r\n")] = '\0';
        char clean[1024];
        size_t c = 0;
        bool non_ascii = false;
        for (const unsigned char *p = (const unsigned char *)line; *p; p++) {
            if (*p < 0x20 || *p == 0x7F)
                continue;   /* 制御文字 */
            if (*p >= 0x80)
                non_ascii = true;
            clean[c++] = (char)*p;
        }
        clean[c] = '\0';
        if (non_ascii && !news.unicode) {
            skipped++;
            continue;
        }
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
    fprintf(stderr, "news: read %s (%zu bytes)\n", news.path, n);
    news_set(n         ? text
             : skipped ? "No Japanese font: install fonts-ipafont-gothic, or start with --font <ttf>"
                       : "news.txt is empty",
             st.st_mtime);
}

static void news_check(lv_timer_t *t)
{
    (void)t;
    news_load();
}

/* 1 秒ごと：ジャンルを選んだあとだけ、ファイルを見る */
static void news_fast_check(lv_timer_t *t)
{
    (void)t;
    if (news.fast_checks > 0) {
        news.fast_checks--;
        news_load();
    }
}

/* ---- ジャンルを選ぶ画面（いくつでも選べる） ---- */

/* fetch_news.py の名前（FEEDS）と、画面に出す名前 */
static const struct { const char *key, *label; } news_genres[] = {
    { "nhk", "Top" },           { "nhk-society", "Society" }, { "nhk-culture", "Culture" },
    { "nhk-science", "Science" }, { "nhk-politics", "Politics" }, { "nhk-business", "Business" },
    { "nhk-world", "World" },   { "nhk-sports", "Sports" },   { "bbc", "BBC World" },
};
#define NEWS_GENRES     ((int)(sizeof news_genres / sizeof news_genres[0]))
#define NEWS_GENRE_COLS 5
static const char *news_map[NEWS_GENRES + NEWS_GENRES / NEWS_GENRE_COLS + 1];
static lv_obj_t *news_matrix;

/* 選んでいるジャンル（ui.conf の news_feeds。空なら fetch_news.py の初期値と同じ主要ニュース） */
static bool news_genre_on(const char *key)
{
    const char *list = settings()->news_feeds[0] ? settings()->news_feeds : "nhk";
    size_t n = strlen(key);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, key, n) == 0)
            return true;
        if (!end)
            break;
        p = end + 1;
    }
    return false;
}

static void news_done_cb(lv_event_t *e)
{
    (void)e;
    char list[sizeof settings()->news_feeds] = "";
    char args[900];
    int a = snprintf(args, sizeof args, "--out \"%s/news.txt\"", settings_dir());
    for (int i = 0; i < NEWS_GENRES; i++) {
        if (!lv_buttonmatrix_has_button_ctrl(news_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED))
            continue;
        size_t n = strlen(list);
        snprintf(list + n, sizeof list - n, "%s%s", n ? "," : "", news_genres[i].key);
        a += snprintf(args + a, sizeof args - (size_t)a, " --feed %s", news_genres[i].key);
    }
    picker_close();
    if (!list[0])
        return;     /* 1 つも選んでいない：前のまま */
    if (strcmp(list, settings()->news_feeds[0] ? settings()->news_feeds : "nhk") == 0)
        return;
    snprintf(settings()->news_feeds, sizeof settings()->news_feeds, "%s", list);
    settings_save();
    fprintf(stderr, "news: genres set to %s\n", list);
    if (refetch("news.request", list, "fetch_news.py", args))
        news.fast_checks = 60;
}

static void news_open_picker(void)
{
    if (picker)
        return;
    int k = 0;
    for (int i = 0; i < NEWS_GENRES; i++) {
        if (i > 0 && i % NEWS_GENRE_COLS == 0)
            news_map[k++] = "\n";
        news_map[k++] = news_genres[i].label;
    }
    news_map[k] = "";
    news_matrix = picker_create("News  " "\xE2\x80\xA2" "  Choose the genres", news_map, 200);
    for (int i = 0; i < NEWS_GENRES; i++)
        if (news_genre_on(news_genres[i].key))
            lv_buttonmatrix_set_button_ctrl(news_matrix, (uint32_t)i, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_t *done = picker_button(lv_obj_get_parent(news_matrix), "Done", news_done_cb);
    lv_obj_align(done, LV_ALIGN_TOP_RIGHT, -220, 0);
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
    lv_obj_t *badge = news.badge = box(page);
    lv_obj_set_size(badge, 120, 64);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, amber, 0);
    lv_obj_set_flex_flow(badge, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(badge, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *title = news.title = label(badge, &lv_font_montserrat_28, lv_color_black());
    lv_label_set_text(title, "NEWS");
    news.stamp = label(badge, &lv_font_montserrat_14, lv_color_black());

    /* 掲示板：黒地に琥珀色の文字を、右から左へ流し続ける */
    lv_obj_t *board = news.board = box(page);
    lv_obj_set_height(board, 64);
    lv_obj_set_flex_grow(board, 1);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x0B0B0B), 0);
    lv_obj_set_style_clip_corner(board, true, 0);
    lv_obj_set_style_pad_hor(board, 12, 0);
    lv_font_t *jp = load_japanese_font(hooks.font_path);
    news.unicode = jp != NULL;
    news.ticker = label(board, jp ? jp : &lv_font_montserrat_40, amber);
    lv_obj_set_width(news.ticker, LV_PCT(100));
    lv_label_set_long_mode(news.ticker, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_align(news.ticker, LV_ALIGN_LEFT_MID, 0, 0);

    news_load();
    lv_timer_create(news_check, NEWS_CHECK_MS, NULL);
    lv_timer_create(news_fast_check, 1000, NULL);
    zone(page, news_open_picker);   /* 叩くとジャンルを選ぶ画面 */
}

/* Plain：黒地に琥珀色の電光掲示板。Picture：背景の絵の上では強すぎるので、ヘッダーと同じ透けた地に白い字にする
 * （札も同じ。ほかのボタンの半透明と揃える） */
#define NEWS_GLASS_OPA  110     /* ヘッダーと同じ（ui.c の create_top） */

void widgets_set_look(bool picture)
{
    if (!news.board)
        return;
    const lv_color_t amber = lv_palette_main(LV_PALETTE_AMBER);
    const lv_color_t text = picture ? lv_color_white() : amber;
    /* Picture の地は、ヘッダーと同じ色（テーマの箱の色）と透け具合にする */
    lv_obj_t *probe = lv_obj_create(news.board);
    const lv_color_t panel = lv_obj_get_style_bg_color(probe, LV_PART_MAIN);
    lv_obj_delete(probe);
    lv_obj_set_style_bg_color(news.board, picture ? panel : lv_color_hex(0x0B0B0B), 0);
    lv_obj_set_style_bg_opa(news.board, picture ? NEWS_GLASS_OPA : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(news.ticker, text, 0);
    lv_obj_set_style_bg_color(news.badge, picture ? panel : amber, 0);
    lv_obj_set_style_bg_opa(news.badge, picture ? NEWS_GLASS_OPA : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(news.title, picture ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_text_color(news.stamp, picture ? lv_color_white() : lv_color_black(), 0);
}

/* ---- Typing：打鍵の速さ ---- */

/* RP2040 の ACT（文字や機能のキーが押された）を数える。修飾キー・レイヤーキーなどは数えない。
 * ACT は、送りきる前に続けて押されたぶんを 1 回にまとめるので、速く打つと少なめに出ることがある */
#define TYPE_BUCKETS    120     /* 1 秒ずつ、直近 2 分 */
#define TYPE_WINDOW_S   15      /* WPM は直近 15 秒の打鍵から */
#define TYPE_BAR_S      5       /* グラフの 1 本は 5 秒 */
#define TYPE_BARS       (TYPE_BUCKETS / TYPE_BAR_S)
#define CHARS_PER_WORD  5

static struct {
    uint16_t buckets[TYPE_BUCKETS];     /* 秒ごとの打鍵数（輪） */
    uint32_t sec;                       /* buckets の最新の秒 */
    uint32_t today, best;
    int yday;
    lv_obj_t *wpm, *today_lbl, *best_lbl, *chart;
    lv_chart_series_t *series;
} typing = { .yday = -1 };

static void typing_advance(void)
{
    uint32_t now = lv_tick_get() / 1000;
    if (now - typing.sec >= TYPE_BUCKETS) {
        memset(typing.buckets, 0, sizeof typing.buckets);
    } else {
        while (typing.sec != now) {
            typing.sec++;
            typing.buckets[typing.sec % TYPE_BUCKETS] = 0;
        }
    }
    typing.sec = now;
    /* 日付が変わったら、今日の数を 0 に戻す */
    struct tm tm;
    platform_localtime(time(NULL), &tm);
    if (tm.tm_yday != typing.yday) {
        typing.yday = tm.tm_yday;
        typing.today = 0;
        typing.best = 0;
    }
}

void widgets_key_activity(void)
{
    typing_advance();
    typing.buckets[typing.sec % TYPE_BUCKETS]++;
    typing.today++;
}

/* 直近 n 秒の打鍵数 */
static uint32_t typing_keys(uint32_t from_sec_ago, uint32_t n)
{
    uint32_t sum = 0;
    for (uint32_t i = from_sec_ago; i < from_sec_ago + n && i < TYPE_BUCKETS; i++)
        /* sec - i は、起動から 2 分たたないうちは 0 より小さくなる（符号なしで桁あふれして、別の箱を指す）
         * ので、箱の番号の上で引く */
        sum += typing.buckets[(typing.sec % TYPE_BUCKETS + TYPE_BUCKETS - i) % TYPE_BUCKETS];
    return sum;
}

static void typing_tick(lv_timer_t *t)
{
    (void)t;
    typing_advance();
    uint32_t wpm = typing_keys(0, TYPE_WINDOW_S) * 60 / TYPE_WINDOW_S / CHARS_PER_WORD;
    if (wpm > typing.best)
        typing.best = wpm;
    lv_label_set_text_fmt(typing.wpm, "%u", (unsigned)wpm);
    lv_obj_set_style_text_opa(typing.wpm, wpm ? LV_OPA_COVER : LV_OPA_50, 0);

    char num[16];
    uint32_t v = typing.today;
    if (v >= 1000)
        snprintf(num, sizeof num, "%u,%03u", (unsigned)(v / 1000), (unsigned)(v % 1000));
    else
        snprintf(num, sizeof num, "%u", (unsigned)v);
    lv_label_set_text_fmt(typing.today_lbl, "Today  %s keys", num);
    lv_label_set_text_fmt(typing.best_lbl, "Best  %u WPM", (unsigned)typing.best);

    /* 直近 2 分のグラフ（左が古い）。1 本は 5 秒の WPM */
    int32_t peak = 60;
    for (int b = 0; b < TYPE_BARS; b++) {
        uint32_t keys = typing_keys((uint32_t)(TYPE_BARS - 1 - b) * TYPE_BAR_S, TYPE_BAR_S);
        int32_t w = (int32_t)(keys * 60 / TYPE_BAR_S / CHARS_PER_WORD);
        if (w > peak)
            peak = w;
        lv_chart_set_value_by_id(typing.chart, typing.series, (uint32_t)b, w);
    }
    lv_chart_set_axis_range(typing.chart, LV_CHART_AXIS_PRIMARY_Y, 0, peak);
    lv_chart_refresh(typing.chart);
}

static void create_typing(lv_obj_t *page)
{
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(page, 24, 0);
    lv_obj_set_style_pad_column(page, 20, 0);

    lv_obj_t *title = label(page, &lv_font_montserrat_28, lv_palette_main(LV_PALETTE_GREY));
    lv_label_set_text(title, LV_SYMBOL_KEYBOARD "  Typing");
    lv_obj_set_width(title, 220);

    typing.wpm = label(page, &lv_font_montserrat_40, lv_color_white());
    lv_obj_set_width(typing.wpm, 90);
    lv_obj_set_style_text_align(typing.wpm, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_t *unit = label(page, &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_GREY));
    lv_label_set_text(unit, "WPM");

    typing.chart = lv_chart_create(page);
    lv_obj_set_clickable(typing.chart, false);
    lv_obj_set_height(typing.chart, 56);
    lv_obj_set_flex_grow(typing.chart, 1);
    lv_chart_set_type(typing.chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(typing.chart, TYPE_BARS);
    lv_chart_set_div_line_count(typing.chart, 0, 0);
    lv_obj_set_style_bg_opa(typing.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(typing.chart, 0, 0);
    lv_obj_set_style_pad_all(typing.chart, 0, 0);
    lv_obj_set_style_pad_column(typing.chart, 4, 0);
    lv_obj_set_style_radius(typing.chart, 2, LV_PART_ITEMS);
    typing.series = lv_chart_add_series(typing.chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *side = box(page);
    lv_obj_set_size(side, 280, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(side, 4, 0);
    typing.today_lbl = label(side, &lv_font_montserrat_20, lv_color_white());
    typing.best_lbl = label(side, &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_GREY));

    typing.sec = lv_tick_get() / 1000;
    typing_tick(NULL);
    lv_timer_create(typing_tick, 500, NULL);
}

/* ---- Weather：天気 ---- */

/* 設定のフォルダの weather.txt（tools/fetch_weather.py が書く）を読んで出す。1 行に 1 つ、名前=値：
 *   place=Tokyo
 *   now=<気温>,<WMO のコード>
 *   today=<最高>,<最低>,<降水確率 %>
 *   tomorrow=<最高>,<最低>,<降水確率 %>,<WMO のコード>
 *   hour=<HH:MM>,<気温>,<WMO のコード>,<降水確率 %>     （数時間おきに 4 つまで）
 */
#define WEATHER_CHECK_MS 30000
#define WEATHER_HOURS    4

typedef struct {
    lv_obj_t *time, *icon, *temp;
} weather_slot_t;

static struct {
    char path[600];
    time_t mtime;
    bool loaded;
    lv_obj_t *icon, *temp, *place, *range, *empty, *row, *now;
    weather_slot_t hours[WEATHER_HOURS], tomorrow;
    int fast_checks;            /* 場所を選んだあと、あと何回 1 秒ごとに見るか */
} weather;

/* 場所の候補（英語の地名。fetch_weather.py が Open-Meteo の地名検索で引く） */
#define WEATHER_CITY_COLS 6
static const char *const weather_cities[] = {
    "Sapporo", "Sendai", "Niigata", "Tokyo", "Yokohama", "Nagoya",
    "Kanazawa", "Kyoto", "Osaka", "Kobe", "Hiroshima", "Fukuoka",
    "Kagoshima", "Naha", "Seoul", "Taipei", "London", "New York",
};
#define WEATHER_CITY_COUNT ((int)(sizeof weather_cities / sizeof weather_cities[0]))

/* 選んでいる場所。選んでいなければ fetch_weather.py の初期値と同じ東京 */
static const char *weather_city(void)
{
    return settings()->weather_city[0] ? settings()->weather_city : "Tokyo";
}

static bool same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char y = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (x != y)
            return false;
    }
    return *a == *b;
}

static lv_color_t weather_color(icon_weather_t kind)
{
    switch (kind) {
    case ICON_WEATHER_CLEAR:
    case ICON_WEATHER_PARTLY:
        return lv_palette_main(LV_PALETTE_AMBER);
    case ICON_WEATHER_RAIN:
    case ICON_WEATHER_THUNDER:
        return lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 1);
    default:
        return lv_palette_lighten(LV_PALETTE_GREY, 2);
    }
}

static void weather_icon(lv_obj_t *img, int code, int size)
{
    icon_weather_t kind = icon_weather_from_wmo(code);
    lv_image_set_src(img, icon_weather(kind, size));
    lv_obj_set_style_image_recolor(img, weather_color(kind), 0);
}

static weather_slot_t weather_slot(lv_obj_t *parent, int width)
{
    weather_slot_t s;
    lv_obj_t *col = box(parent);
    lv_obj_set_size(col, width, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(col, 10, 0);
    s.time = label(col, &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_GREY));
    s.icon = icon_create(col, icon_weather(ICON_WEATHER_CLOUDY, 36), lv_color_white());
    s.temp = label(col, &lv_font_montserrat_20, lv_color_white());
    return s;
}

static void weather_show_empty(const char *msg)
{
    lv_obj_set_hidden(weather.row, true);
    lv_obj_set_hidden(weather.empty, false);
    lv_label_set_text(weather.empty, msg);
}

static void weather_load(void)
{
    struct stat st;
    if (stat(weather.path, &st) != 0) {
        if (weather.loaded || weather.mtime != (time_t)-1) {
            weather.loaded = false;
            weather.mtime = (time_t)-1;
            weather_show_empty("No weather yet.  Run  tools/fetch_weather.py  (weather.txt next to ui.conf)");
        }
        return;
    }
    if (weather.loaded && st.st_mtime == weather.mtime)
        return;
    FILE *f = fopen(weather.path, "r");
    if (!f)
        return;
    weather.mtime = st.st_mtime;
    weather.loaded = true;
    fprintf(stderr, "weather: read %s\n", weather.path);

    char line[256], place[64] = "", city[48] = "";
    float now_t = 0, hi = 0, lo = 0, thi = 0, tlo = 0;
    int now_code = 3, rain = 0, train = 0, tcode = 3, hours = 0;
    bool have_now = false, have_today = false, have_tomorrow = false;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char hhmm[8];
        float t;
        int code, pop;
        if (strncmp(line, "place=", 6) == 0) {
            /* 英数字だけ（Montserrat で描くため） */
            size_t n = 0;
            for (const char *p = line + 6; *p && n + 1 < sizeof place; p++)
                if (*p >= 0x20 && *p < 0x7F)
                    place[n++] = *p;
            place[n] = '\0';
        } else if (strncmp(line, "city=", 5) == 0) {
            snprintf(city, sizeof city, "%s", line + 5);
        } else if (sscanf(line, "now=%f,%d", &now_t, &now_code) == 2) {
            have_now = true;
        } else if (sscanf(line, "today=%f,%f,%d", &hi, &lo, &rain) == 3) {
            have_today = true;
        } else if (sscanf(line, "tomorrow=%f,%f,%d,%d", &thi, &tlo, &train, &tcode) == 4) {
            have_tomorrow = true;
        } else if (hours < WEATHER_HOURS && sscanf(line, "hour=%5[0-9:],%f,%d,%d", hhmm, &t, &code, &pop) == 4) {
            weather_slot_t *s = &weather.hours[hours++];
            lv_label_set_text(s->time, hhmm);
            weather_icon(s->icon, code, 36);
            lv_label_set_text_fmt(s->temp, "%d\xC2\xB0", (int)lroundf(t));
        }
    }
    fclose(f);
    if (!have_now) {
        weather_show_empty("weather.txt has no current weather");
        return;
    }
    /* 場所を選び直したあと、まだ前の場所の天気のまま（city= は fetch_weather.py が、引いた地名を書く。
     * --lat / --lon で取ったときは書かないので、選んだ場所とは比べない） */
    if (city[0] && !same_name(city, weather_city())) {
        /* 選んだあとしばらくは待つ。それでも届かなければ（ネットにつながっていないなど）そう出す */
        char msg[160];
        if (weather.fast_checks > 0)
            snprintf(msg, sizeof msg, "%s " LV_SYMBOL_DOWN "     Waiting for the weather", weather_city());
        else
            snprintf(msg, sizeof msg, "%s " LV_SYMBOL_DOWN "     Could not get the weather yet  "
                     "(network?  tools/fetch_weather.py)", weather_city());
        weather_show_empty(msg);
        return;
    }
    lv_obj_set_hidden(weather.empty, true);
    lv_obj_set_hidden(weather.row, false);

    weather_icon(weather.icon, now_code, 56);
    lv_label_set_text_fmt(weather.temp, "%d\xC2\xB0", (int)lroundf(now_t));
    struct tm tm;
    platform_localtime(st.st_mtime, &tm);
    lv_label_set_text_fmt(weather.place, "%s " LV_SYMBOL_DOWN "  " "\xE2\x80\xA2" "  %02d:%02d", place[0] ? place : "Weather",
                          tm.tm_hour, tm.tm_min);
    if (have_today)
        lv_label_set_text_fmt(weather.range, "H %d\xC2\xB0  L %d\xC2\xB0  Rain %d%%", (int)lroundf(hi),
                              (int)lroundf(lo), rain);
    else
        lv_label_set_text(weather.range, "");
    for (int i = 0; i < WEATHER_HOURS; i++) {
        lv_obj_t *slot = lv_obj_get_parent(weather.hours[i].time);
        lv_obj_set_hidden(slot, i >= hours);
    }
    lv_obj_set_hidden(lv_obj_get_parent(weather.tomorrow.time), !have_tomorrow);
    if (have_tomorrow) {
        weather_icon(weather.tomorrow.icon, tcode, 36);
        lv_label_set_text_fmt(weather.tomorrow.temp, "%d\xC2\xB0 / %d\xC2\xB0  %d%%", (int)lroundf(thi),
                              (int)lroundf(tlo), train);
    }
}

static void weather_check(lv_timer_t *t)
{
    (void)t;
    weather_load();
}

/* 1 秒ごと：場所を選んだあとだけ、ファイルを見る */
static void weather_fast_check(lv_timer_t *t)
{
    (void)t;
    if (weather.fast_checks > 0) {
        weather.fast_checks--;
        if (weather.fast_checks == 0)
            weather.loaded = false;     /* 待ち終わった：まだ届いていなければ、出す文を替える */
        weather_load();
    }
}

/* ---- 場所を選ぶ画面 ---- */

static const char *picker_map[WEATHER_CITY_COUNT + WEATHER_CITY_COUNT / WEATHER_CITY_COLS + 1];
static char picker_custom[48];      /* ui.conf に、候補にない場所が書いてあるとき */

/* 選んだ：ui.conf に覚え、取り直す */
static void weather_choose(const char *name)
{
    picker_close();
    if (same_name(name, weather_city()))
        return;
    snprintf(settings()->weather_city, sizeof settings()->weather_city, "%s", name);
    settings_save();
    fprintf(stderr, "weather: place set to %s\n", name);
    char args[700];
    snprintf(args, sizeof args, "--city \"%s\" --out \"%s/weather.txt\"", name, settings_dir());
    if (refetch("weather.request", name, "fetch_weather.py", args))
        weather.fast_checks = 60;   /* 届いたらすぐ出すよう、しばらく 1 秒ごとにファイルを見る */
    weather.loaded = false;     /* 同じファイルでも、選んだ場所と比べ直す */
    weather_load();
}

static void picker_city_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    const char *name = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
    if (name)
        weather_choose(name);
}

static void weather_open_picker(void)
{
    if (picker)
        return;
    /* 並べる候補。ui.conf に候補にない場所が書いてあれば、最後の 1 つの代わりに出す */
    const char *cur = weather_city();
    bool listed = false;
    for (int i = 0; i < WEATHER_CITY_COUNT; i++)
        listed |= same_name(cur, weather_cities[i]);
    int k = 0, checked = -1;
    for (int i = 0; i < WEATHER_CITY_COUNT; i++) {
        if (i > 0 && i % WEATHER_CITY_COLS == 0)
            picker_map[k++] = "\n";
        const char *name = weather_cities[i];
        if (!listed && i == WEATHER_CITY_COUNT - 1) {
            snprintf(picker_custom, sizeof picker_custom, "%s", cur);
            name = picker_custom;
        }
        if (same_name(name, cur))
            checked = i;
        picker_map[k++] = name;
    }
    picker_map[k] = "";

    lv_obj_t *m = picker_create("Weather  " "\xE2\x80\xA2" "  Choose the place", picker_map, 300);
    lv_buttonmatrix_set_one_checked(m, true);
    if (checked >= 0)
        lv_buttonmatrix_set_button_ctrl(m, (uint32_t)checked, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_add_event_cb(m, picker_city_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_weather(lv_obj_t *page)
{
    snprintf(weather.path, sizeof weather.path, "%s/weather.txt", settings_dir());

    weather.empty = label(page, &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_GREY));
    lv_obj_align(weather.empty, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_set_style_pad_hor(weather.empty, 12, 0);
    lv_obj_set_style_pad_ver(weather.empty, 22, 0);
    zone(weather.empty, weather_open_picker);   /* 天気がまだないときも、ここから場所を選べる */

    weather.row = box(page);
    lv_obj_set_size(weather.row, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(weather.row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(weather.row, 12, 0);
    lv_obj_set_style_pad_right(weather.row, 24, 0);
    lv_obj_set_style_pad_column(weather.row, 16, 0);

    /* いま：アイコン、気温、場所と今日の最高・最低・降水確率。叩くと場所を選ぶ画面 */
    weather.now = box(weather.row);
    lv_obj_set_size(weather.now, LV_SIZE_CONTENT, 72);
    lv_obj_set_flex_flow(weather.now, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(weather.now, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(weather.now, 12, 0);
    lv_obj_set_style_pad_column(weather.now, 16, 0);
    zone(weather.now, weather_open_picker);
    weather.icon = icon_create(weather.now, icon_weather(ICON_WEATHER_CLOUDY, 56), lv_color_white());
    weather.temp = label(weather.now, &lv_font_montserrat_40, lv_color_white());
    lv_obj_set_width(weather.temp, 100);
    lv_obj_t *info = box(weather.now);
    lv_obj_set_size(info, 380, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 2, 0);
    weather.place = label(info, &lv_font_montserrat_20, lv_color_white());
    weather.range = label(info, &lv_font_montserrat_20, lv_palette_main(LV_PALETTE_GREY));

    /* この先の数時間 */
    for (int i = 0; i < WEATHER_HOURS; i++)
        weather.hours[i] = weather_slot(weather.row, 190);

    /* あした */
    lv_obj_t *gap = box(weather.row);
    lv_obj_set_flex_grow(gap, 1);
    weather.tomorrow = weather_slot(weather.row, LV_SIZE_CONTENT);
    lv_label_set_text(weather.tomorrow.time, "Tomorrow");

    weather.mtime = 0;
    weather_load();
    lv_timer_create(weather_check, WEATHER_CHECK_MS, NULL);
    lv_timer_create(weather_fast_check, 1000, NULL);
}

/* ---- 並びと、スワイプでの切り替え ---- */

static void layout(void)
{
    /* いまのウィジェットを offset の位置に置き、ずれた側に隣を出す。並びは輪になっていて、いちばん右の
     * 右は None、None の左はいちばん右。2 つしかないときは左右の隣が同じなので、ずれた側にだけ出す */
    for (int i = 0; i < WIDGET_COUNT; i++)
        lv_obj_set_hidden(pages[i], true);
    lv_obj_t *cur = pages[order[current]];
    lv_obj_set_hidden(cur, false);
    lv_obj_set_x(cur, offset);
    if (order_count < 2 || offset == 0)
        return;
    int dir = offset < 0 ? 1 : -1;      /* 左へずれたら右の隣、右へずれたら左の隣 */
    lv_obj_t *next = pages[order[(current + dir + order_count) % order_count]];
    lv_obj_set_hidden(next, false);
    lv_obj_set_x(next, offset + dir * STRIP_W);
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

/* dir：1 で右の隣へ、-1 で左の隣へ（輪になっていて、端から反対の端へつながる）、0 でいまの位置に戻す */
static void slide_by(int dir)
{
    if (order_count < 2)
        dir = 0;
    /* いまの位置（offset）から、見た目がつながるように滑らせる */
    offset += dir * STRIP_W;
    current = (current + dir + order_count) % order_count;
    update_dots();
    if (dir && settings()->shown_widget != (int)order[current]) {
        /* 次に起動したときも、同じウィジェットから */
        settings()->shown_widget = (int)order[current];
        settings_save();
    }
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
    /* 初めて（起動したとき）は、前に出していたもの（設定に保存してある） */
    widget_id_t shown = order_count ? order[current] : (widget_id_t)settings()->shown_widget;
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
    create_typing(pages[WIDGET_TYPING]);
    create_weather(pages[WIDGET_WEATHER]);

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

/* 出しているウィジェットの中にあって、隠れていないか */
static bool shown_in_current(lv_obj_t *obj)
{
    for (lv_obj_t *o = obj; o; o = lv_obj_get_parent(o)) {
        if (o == pages[order[current]])
            return true;
        if (lv_obj_is_hidden(o))
            return false;
    }
    return false;
}

static wbutton_t *button_at(const lv_point_t *p)
{
    for (int i = 0; i < button_count; i++) {
        lv_obj_t *b = buttons[i].obj;
        if (shown_in_current(b) && inside(b, p))
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
        slide_by(0);
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
            /* 隣は 1 つ分だけ出すので、ずれは 1 画面分の手前まで */
            offset = LV_CLAMP(-(STRIP_W - 1), touch.base + dx, STRIP_W - 1);
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
        /* 左へはらう：右のウィジェットへ（いちばん右からは None へ戻る） */
        slide_by(dx <= -SWIPE_MIN ? 1 : dx >= SWIPE_MIN ? -1 : 0);
        return;
    }
    wbutton_t *b = touch.pressed;
    set_pressed(NULL);
    if (b && b->cb)
        b->cb();
}

/* ---- 録画（recorder.c）から ---- */

void widgets_demo_swipe(int dir)
{
    slide_by(dir < 0 ? -1 : dir > 0 ? 1 : 0);
}

void widgets_demo_pomodoro(bool down)
{
    if (down) {
        lv_obj_add_state(pomo.start->obj, LV_STATE_PRESSED);
    } else {
        lv_obj_remove_state(pomo.start->obj, LV_STATE_PRESSED);
        pomo_start_pause();
    }
}

void widgets_demo_weather(const char *arg)
{
    if (strcmp(arg, "open") == 0)
        weather_open_picker();
    else
        weather_choose(arg);
}
