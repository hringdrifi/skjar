#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define SEP "\\"
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#define SEP "/"
#endif

static settings_t current = { .return_mode = RETURN_KEY_OPENED };
static char file_path[512];
static char dir_path[512];

static const char *const return_names[] = { "never", "key", "always" };

static void default_path(void)
{
#if defined(_WIN32)
    const char *base = getenv("APPDATA");
    snprintf(dir_path, sizeof dir_path, "%s" SEP "skjar", base ? base : ".");
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(dir_path, sizeof dir_path, "%s/skjar", xdg);
    else
        snprintf(dir_path, sizeof dir_path, "%s/.config/skjar", home ? home : ".");
#endif
    snprintf(file_path, sizeof file_path, "%s" SEP "ui.conf", dir_path);
}

void settings_load(const char *path)
{
    if (path) {
        snprintf(file_path, sizeof file_path, "%s", path);
        dir_path[0] = '\0';
    } else {
        default_path();
    }

    FILE *f = fopen(file_path, "r");
    if (!f)
        return;     /* まだ保存していない。初期値のまま */
    char line[128];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *key = line, *val = eq + 1;
        if (strcmp(key, "return_mode") == 0) {
            for (int i = 0; i < 3; i++)
                if (strcmp(val, return_names[i]) == 0)
                    current.return_mode = (return_mode_t)i;
        }
    }
    fclose(f);
    fprintf(stderr, "settings: loaded %s\n", file_path);
}

void settings_save(void)
{
    if (dir_path[0]) {
        /* 1 段上（~/.config）がないときもあるので、2 段まで作る */
        char parent[512];
        snprintf(parent, sizeof parent, "%s", dir_path);
        char *last = strrchr(parent, SEP[0]);
        if (last) {
            *last = '\0';
            MKDIR(parent);
        }
        MKDIR(dir_path);
    }
    FILE *f = fopen(file_path, "w");
    if (!f) {
        fprintf(stderr, "settings: cannot write %s\n", file_path);
        return;
    }
    fprintf(f, "return_mode=%s\n", return_names[current.return_mode]);
    fclose(f);
}

settings_t *settings(void)
{
    return &current;
}
