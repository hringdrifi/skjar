#include "movie.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "link.h"
#include "platform.h"

/* LVGL の画像の覚え（キャッシュ）から、中身を書き換えた画像を外す（公開の見出しにない関数） */
void lv_image_cache_drop(const void *src);

#define SCALE    2              /* 320 × 240 を 2 倍にして出す */
#define KEYS     42
#define HDR_SIZE 32

/* キーの中心（基板の座標、mm）。RMK のマトリクスの順（tools/make_movie.py と同じ） */
static const float key_pos[KEYS][2] = {
    { 43.656f, 95.25f }, { 62.706f, 95.25f }, { 81.756f, 95.25f }, { 100.806f, 95.25f }, { 119.856f, 95.25f }, { 138.906f, 95.25f },
    { 253.206f, 95.25f }, { 234.156f, 95.25f }, { 215.106f, 95.25f }, { 196.056f, 95.25f }, { 177.006f, 95.25f }, { 157.956f, 95.25f },
    { 46.038f, 114.3f }, { 67.469f, 114.3f }, { 86.519f, 114.3f }, { 105.569f, 114.3f }, { 124.619f, 114.3f }, { 143.669f, 114.3f },
    { 250.825f, 152.4f }, { 246.062f, 114.3f }, { 219.869f, 114.3f }, { 200.819f, 114.3f }, { 181.769f, 114.3f }, { 162.719f, 114.3f },
    { 50.8f, 133.35f }, { 76.994f, 133.35f }, { 96.044f, 133.35f }, { 115.094f, 133.35f }, { 134.144f, 133.35f }, { 153.194f, 133.35f },
    { 229.394f, 152.4f }, { 250.825f, 133.35f }, { 229.394f, 133.35f }, { 210.344f, 133.35f }, { 191.294f, 133.35f }, { 172.244f, 133.35f },
    { 46.038f, 152.4f }, { 67.469f, 152.4f }, { 88.9f, 152.4f }, { 207.963f, 152.4f }, { 127.0f, 152.4f }, { 174.625f, 152.4f },
};
/* キーの幅（u）。広いキーは見本でも広く描く */
static const float key_w[KEYS] = {
    1, 1, 1, 1, 1, 1,  1, 1, 1, 1, 1, 1,  1.25f, 1, 1, 1, 1, 1,  1.25f, 1.75f, 1, 1, 1, 1,
    1.75f, 1, 1, 1, 1, 1,  1, 1.25f, 1, 1, 1, 1,  1.25f, 1, 1.25f, 1.25f, 2.75f, 2.25f,
};

static uint8_t *file;           /* .skm を丸ごと */
static size_t file_size;
static uint16_t fps, width, height;
static uint32_t frames;
static const uint32_t *index_table;

static lv_obj_t *overlay, *video, *led_box[KEYS];
static lv_image_dsc_t video_dsc;
static uint8_t *pixels;         /* L8、width × SCALE × height × SCALE */
static lv_timer_t *timer;
static uint32_t start_ms;
static int32_t shown = -1;
static char wav_path[600];

bool movie_playing(void)
{
    return overlay != NULL;
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24); }

static bool load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "movie: cannot open %s\n", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < HDR_SIZE) {
        fclose(f);
        return false;
    }
    file = malloc((size_t)size);
    file_size = (size_t)size;
    bool ok = file && fread(file, 1, file_size, f) == file_size;
    fclose(f);
    if (!ok || memcmp(file, "SKJM", 4) != 0 || rd16(file + 4) != 1 || file[13] != KEYS) {
        fprintf(stderr, "movie: %s is not a SKJAR movie (make it with tools/make_movie.py)\n", path);
        free(file);
        file = NULL;
        return false;
    }
    fps = rd16(file + 6);
    width = rd16(file + 8);
    height = rd16(file + 10);
    frames = rd32(file + 14);
    index_table = (const uint32_t *)(file + HDR_SIZE);
    if (fps == 0 || HDR_SIZE + (size_t)frames * 4 > file_size) {
        free(file);
        file = NULL;
        return false;
    }
    return true;
}

/* コマ n の画面を、2 倍にして pixels に書く。キーのライト（0〜15）を leds に入れる */
static void decode(uint32_t n, uint8_t leds[KEYS])
{
    uint32_t off = rd32((const uint8_t *)&index_table[n]);
    if (off + 25 > file_size)
        return;
    const uint8_t *p = file + off;
    for (int i = 0; i < KEYS; i++)
        leds[i] = (i & 1) ? (p[i / 2] >> 4) : (p[i / 2] & 0x0F);
    uint32_t len = rd32(p + 21);
    const uint8_t *rle = p + 25;
    if (off + 25 + len > file_size)
        return;

    static const uint8_t level_gray[4] = { 0, 85, 170, 255 };
    uint32_t w2 = (uint32_t)width * SCALE;
    uint32_t x = 0, y = 0;
    for (uint32_t i = 0; i < len && y < height; i++) {
        uint8_t g = level_gray[rle[i] >> 6];
        uint32_t run = (rle[i] & 0x3F) + 1u;
        while (run > 0 && y < height) {
            uint32_t n_here = run < width - x ? run : width - x;
            uint8_t *row = pixels + (size_t)y * SCALE * w2 + x * SCALE;
            memset(row, g, n_here * SCALE);
            memset(row + w2, g, n_here * SCALE);    /* 縦も 2 倍 */
            x += n_here;
            run -= n_here;
            if (x == width) {
                x = 0;
                y++;
            }
        }
    }
}

static void show_leds(const uint8_t leds[KEYS])
{
    for (int i = 0; i < KEYS; i++) {
        uint8_t v = (uint8_t)(leds[i] * 17);
        lv_obj_set_style_bg_color(led_box[i], lv_color_make(v, v, v), 0);
    }
    link_send_leds(leds);
}

static void tick(lv_timer_t *t)
{
    (void)t;
    uint32_t elapsed = platform_now_ms() - start_ms;
    uint32_t n = (uint32_t)((uint64_t)elapsed * fps / 1000u);
    if (n >= frames) {
        movie_stop();
        return;
    }
    if ((int32_t)n == shown)
        return;
    shown = (int32_t)n;
    uint8_t leds[KEYS];
    decode(n, leds);
    lv_image_cache_drop(&video_dsc);
    lv_obj_invalidate(video);
    show_leds(leds);
}

#define IGNORE_TAPS_MS 1000     /* 始まってすぐの叩き（時計を 1 回多く叩いた、など）では止めない */

static void overlay_clicked(lv_event_t *e)
{
    (void)e;
    if (platform_now_ms() - start_ms < IGNORE_TAPS_MS)
        return;
    movie_stop();
}

/* 左側に、キーボードの並びを小さく描く（キーのライトの見本） */
static void create_led_preview(lv_obj_t *parent, int left, int width_px)
{
    const float x0 = 34.131f, y0 = 85.725f, board_w = 228.6f, board_h = 76.2f;
    float s = (float)width_px / board_w;         /* mm → px */
    int top = (480 - (int)(board_h * s)) / 2;
    int unit = (int)(19.05f * s);
    for (int i = 0; i < KEYS; i++) {
        int w = (int)(key_w[i] * unit) - 6, h = unit - 6;
        int cx = left + (int)((key_pos[i][0] - x0) * s), cy = top + (int)((key_pos[i][1] - y0) * s);
        lv_obj_t *b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, w, h);
        lv_obj_set_pos(b, cx - w / 2, cy - h / 2);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, lv_color_black(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x303030), 0);
        lv_obj_set_clickable(b, false);
        led_box[i] = b;
    }
}

bool movie_start(const char *path)
{
    if (overlay)
        return true;
    if (!load(path))
        return false;

    uint32_t w2 = (uint32_t)width * SCALE, h2 = (uint32_t)height * SCALE;
    pixels = calloc(1, (size_t)w2 * h2);
    if (!pixels) {
        free(file);
        file = NULL;
        return false;
    }
    memset(&video_dsc, 0, sizeof video_dsc);
    video_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    video_dsc.header.cf = LV_COLOR_FORMAT_L8;
    video_dsc.header.w = w2;
    video_dsc.header.h = h2;
    video_dsc.header.stride = w2;
    video_dsc.data_size = w2 * h2;
    video_dsc.data = pixels;

    /* 一番手前の層に、全画面の黒い面 */
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_add_event_cb(overlay, overlay_clicked, LV_EVENT_CLICKED, NULL);

    video = lv_image_create(overlay);
    lv_image_set_src(video, &video_dsc);
    lv_obj_center(video);
    lv_obj_set_clickable(video, false);

    create_led_preview(overlay, 20, 600);

    lv_obj_t *hint = lv_label_create(overlay);
    lv_label_set_text(hint, "tap to stop");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x606060), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_RIGHT, -24, -16);

    /* 音は、動画と同じ名前の .wav（なければ映像だけ） */
    snprintf(wav_path, sizeof wav_path, "%s", path);
    char *dot = strrchr(wav_path, '.');
    if (dot)
        snprintf(dot, sizeof wav_path - (size_t)(dot - wav_path), ".wav");
    platform_audio_play(wav_path);

    shown = -1;
    start_ms = platform_now_ms();
    timer = lv_timer_create(tick, 5, NULL);
    fprintf(stderr, "movie: %u frames at %u fps\n", (unsigned)frames, (unsigned)fps);
    return true;
}

void movie_stop(void)
{
    if (!overlay)
        return;
    platform_audio_stop();
    lv_timer_delete(timer);
    timer = NULL;
    /* 叩いたときのイベントの中から呼ばれるので、面は後で消す（子の画像と見本のキーも消える）。
     * 画素はすぐ解放するので、その前に画像を外し、面を隠しておく */
    lv_image_set_src(video, NULL);
    lv_obj_set_hidden(overlay, true);
    lv_obj_delete_async(overlay);
    overlay = NULL;
    video = NULL;
    lv_image_cache_drop(&video_dsc);
    free(pixels);
    pixels = NULL;
    free(file);
    file = NULL;
    /* キーのライトは、RP2040 が LEDF が途絶えたら自分で消す（ここでは何も送らない） */
}
