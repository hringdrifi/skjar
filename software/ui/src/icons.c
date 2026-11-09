#include "icons.h"

#include <math.h>
#include <string.h>

#define MAX_SIZE 64
#define SUB      4      /* 1 px を 4 × 4 に分けて、縁をなめらかにする */

/* 点 (px, py) から線分 (ax, ay)-(bx, by) までの距離 */
static float dist_to_segment(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay;
    float t = ((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return sqrtf(dx * dx + dy * dy);
}

/* 太陽：中央の円と、8 本の光線（先の丸い線）。寸法は 1 辺に対する割合 */
static bool in_sun(float x, float y, float size)
{
    float c = size / 2.0f;
    float dx = x - c, dy = y - c;
    if (sqrtf(dx * dx + dy * dy) <= size * 0.22f)
        return true;
    float r0 = size * 0.34f, r1 = size * 0.46f, half_w = size * 0.045f;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 3.14159265f / 4.0f;
        float ca = cosf(a), sa = sinf(a);
        if (dist_to_segment(x, y, c + ca * r0, c + sa * r0, c + ca * r1, c + sa * r1) <= half_w)
            return true;
    }
    return false;
}

static uint8_t sun_px[MAX_SIZE * MAX_SIZE];
static lv_image_dsc_t sun_dsc;

const lv_image_dsc_t *icon_sun(int size)
{
    if (size > MAX_SIZE)
        size = MAX_SIZE;
    if (sun_dsc.header.w == (uint32_t)size)
        return &sun_dsc;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int sy = 0; sy < SUB; sy++)
                for (int sx = 0; sx < SUB; sx++)
                    hits += in_sun(x + (sx + 0.5f) / SUB, y + (sy + 0.5f) / SUB, (float)size);
            sun_px[y * size + x] = (uint8_t)(hits * 255 / (SUB * SUB));
        }
    }

    memset(&sun_dsc, 0, sizeof sun_dsc);
    sun_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    sun_dsc.header.cf = LV_COLOR_FORMAT_A8;
    sun_dsc.header.w = (uint32_t)size;
    sun_dsc.header.h = (uint32_t)size;
    sun_dsc.header.stride = (uint32_t)size;
    sun_dsc.data_size = (uint32_t)(size * size);
    sun_dsc.data = sun_px;
    return &sun_dsc;
}

lv_obj_t *icon_create(lv_obj_t *parent, const lv_image_dsc_t *icon, lv_color_t color)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, icon);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}
