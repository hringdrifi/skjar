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

/* 点 (x, y) が、中心 (cx, cy)・半径 r の円の中か（どれも 1 辺に対する割合） */
static bool in_circle(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

/* 太陽：中央の円と、8 本の光線（先の丸い線）。座標は 1 辺を 1 とした割合 */
static bool in_sun(float x, float y)
{
    if (in_circle(x, y, 0.5f, 0.5f, 0.22f))
        return true;
    const float r0 = 0.34f, r1 = 0.46f, half_w = 0.045f;
    for (int i = 0; i < 8; i++) {
        float a = (float)i * 3.14159265f / 4.0f;
        float ca = cosf(a), sa = sinf(a);
        if (dist_to_segment(x, y, 0.5f + ca * r0, 0.5f + sa * r0, 0.5f + ca * r1, 0.5f + sa * r1) <= half_w)
            return true;
    }
    return false;
}

/* パレット（見た目）：丸い板の右下に親指の切り欠き、絵の具の穴が 4 つ */
static bool in_palette(float x, float y)
{
    if (!in_circle(x, y, 0.5f, 0.5f, 0.46f))
        return false;
    if (in_circle(x, y, 0.80f, 0.76f, 0.17f))          /* 親指の切り欠き */
        return false;
    static const float dots[][2] = { { 0.30f, 0.34f }, { 0.50f, 0.22f }, { 0.71f, 0.34f }, { 0.28f, 0.58f } };
    for (size_t i = 0; i < sizeof dots / sizeof dots[0]; i++)
        if (in_circle(x, y, dots[i][0], dots[i][1], 0.085f))
            return false;
    return true;
}

typedef struct {
    bool (*inside)(float x, float y);
    uint8_t px[MAX_SIZE * MAX_SIZE];
    lv_image_dsc_t dsc;
} icon_t;

static icon_t sun = { .inside = in_sun };
static icon_t palette = { .inside = in_palette };

/* 形の関数から、A8 の画像を描く（同じ大きさなら描き直さない） */
static const lv_image_dsc_t *render(icon_t *icon, int size)
{
    if (size > MAX_SIZE)
        size = MAX_SIZE;
    if (icon->dsc.header.w == (uint32_t)size)
        return &icon->dsc;

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            int hits = 0;
            for (int sy = 0; sy < SUB; sy++)
                for (int sx = 0; sx < SUB; sx++)
                    hits += icon->inside((x + (sx + 0.5f) / SUB) / size, (y + (sy + 0.5f) / SUB) / size);
            icon->px[y * size + x] = (uint8_t)(hits * 255 / (SUB * SUB));
        }
    }

    memset(&icon->dsc, 0, sizeof icon->dsc);
    icon->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon->dsc.header.cf = LV_COLOR_FORMAT_A8;
    icon->dsc.header.w = (uint32_t)size;
    icon->dsc.header.h = (uint32_t)size;
    icon->dsc.header.stride = (uint32_t)size;
    icon->dsc.data_size = (uint32_t)(size * size);
    icon->dsc.data = icon->px;
    return &icon->dsc;
}

const lv_image_dsc_t *icon_sun(int size)
{
    return render(&sun, size);
}

const lv_image_dsc_t *icon_palette(int size)
{
    return render(&palette, size);
}

lv_obj_t *icon_create(lv_obj_t *parent, const lv_image_dsc_t *icon, lv_color_t color)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, icon);
    lv_obj_set_style_image_recolor(img, color, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    return img;
}
