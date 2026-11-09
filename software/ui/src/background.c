#include "background.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 1920
#define H 480

static lv_obj_t *image;
static lv_image_dsc_t aurora_dsc;
static uint8_t *aurora_px;
static char png_src[512];

/* ぼんやりした光のかたまり（中心、広がり、色） */
typedef struct {
    float cx, cy, rx, ry;
    float r, g, b;
} glow_t;

static const glow_t glows[] = {
    {  380, 120, 520, 260, 0.05f, 0.55f, 0.55f },   /* 青緑 */
    { 1050, 380, 640, 240, 0.40f, 0.18f, 0.62f },   /* 紫 */
    { 1600,  90, 480, 230, 0.10f, 0.32f, 0.75f },   /* 青 */
    {  760, 300, 300, 160, 0.10f, 0.45f, 0.40f },   /* 青緑（小） */
};

static float clamp01(float v)
{
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

/* 段差（バンディング）を目立たなくするための、小さなゆらぎ */
static float dither(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((float)(h & 0xFF) / 255.0f - 0.5f) / 255.0f;
}

static void draw_aurora(void)
{
    aurora_px = lv_malloc((size_t)W * H * 4);
    if (!aurora_px)
        return;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            /* 下地：左上から右下へ、暗い紺から少し明るい紺へ */
            float t = ((float)x / W + (float)y / H) * 0.5f;
            float r = 0.035f + 0.03f * t, g = 0.045f + 0.035f * t, b = 0.09f + 0.06f * t;
            for (size_t i = 0; i < sizeof glows / sizeof glows[0]; i++) {
                const glow_t *gl = &glows[i];
                float dx = (x - gl->cx) / gl->rx, dy = (y - gl->cy) / gl->ry;
                float w = expf(-(dx * dx + dy * dy)) * 0.55f;
                r += gl->r * w;
                g += gl->g * w;
                b += gl->b * w;
            }
            float d = dither(x, y);
            uint8_t *p = &aurora_px[((size_t)y * W + x) * 4];
            p[0] = (uint8_t)(clamp01(b + d) * 255.0f + 0.5f);     /* XRGB8888 は B G R X の順 */
            p[1] = (uint8_t)(clamp01(g + d) * 255.0f + 0.5f);
            p[2] = (uint8_t)(clamp01(r + d) * 255.0f + 0.5f);
            p[3] = 0xFF;
        }
    }
    memset(&aurora_dsc, 0, sizeof aurora_dsc);
    aurora_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    aurora_dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    aurora_dsc.header.w = W;
    aurora_dsc.header.h = H;
    aurora_dsc.header.stride = W * 4;
    aurora_dsc.data_size = (uint32_t)W * H * 4;
    aurora_dsc.data = aurora_px;
}

void background_create(lv_obj_t *screen, const char *png_path)
{
    image = lv_image_create(screen);
    lv_obj_set_ignore_layout(image, true);               /* 画面の縦の並びに入れない */
    lv_obj_set_size(image, W, H);
    lv_obj_set_pos(image, 0, 0);
    lv_obj_set_clickable(image, false);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_COVER);
    lv_obj_move_to_index(image, 0);                      /* 一番奥 */

    if (png_path) {
        /* LVGL のファイルの読み出し（stdio）は、頭に 'A:' を付けた名前で開く */
        snprintf(png_src, sizeof png_src, "A:%s", png_path);
        lv_image_set_src(image, png_src);
        fprintf(stderr, "background: %s\n", png_path);
    } else {
        draw_aurora();
        if (aurora_px)
            lv_image_set_src(image, &aurora_dsc);
    }
    lv_obj_set_hidden(image, true);
}

void background_show(bool on)
{
    if (image)
        lv_obj_set_hidden(image, !on);
}
