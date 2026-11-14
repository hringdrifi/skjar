#include "backlight.h"

#include <stdio.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#endif

static char brightness_path[300];
static int max_brightness = -1;
static int level = 255;
static int limit = 255;
static int written = -1;

#if defined(_WIN32)

/* Windows（PC で画面を試すとき）：パネルはないので、値を覚えるだけ */
bool backlight_open(void)
{
    fprintf(stderr, "backlight: not available on Windows\n");
    return false;
}

#else

bool backlight_open(void)
{
    DIR *d = opendir("/sys/class/backlight");
    if (!d) {
        fprintf(stderr, "backlight: no /sys/class/backlight\n");
        return false;
    }
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char path[300];
        snprintf(path, sizeof path, "/sys/class/backlight/%s/max_brightness", e->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        int m;
        if (fscanf(f, "%d", &m) == 1 && m > 0) {
            max_brightness = m;
            snprintf(brightness_path, sizeof brightness_path, "/sys/class/backlight/%s/brightness", e->d_name);
            found = true;
        }
        fclose(f);
        if (found) {
            fprintf(stderr, "backlight: %s (max %d)\n", e->d_name, m);
            break;
        }
    }
    closedir(d);
    if (!found)
        fprintf(stderr, "backlight: no device found\n");
    return found;
}

#endif

int backlight_effective(void)
{
    return level < limit ? level : limit;
}

static void apply(void)
{
    int v = backlight_effective();
    if (max_brightness <= 0 || v == written)
        return;
    FILE *f = fopen(brightness_path, "w");
    if (!f) {
        perror("backlight: open brightness");
        return;
    }
    fprintf(f, "%d", (v * max_brightness + 127) / 255);
    fclose(f);
    written = v;
}

static int clamp255(int v)
{
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

void backlight_set_level(int l)
{
    level = clamp255(l);
    apply();
}

void backlight_set_limit(int l)
{
    limit = clamp255(l);
    apply();
}

int backlight_level(void)
{
    return level;
}
