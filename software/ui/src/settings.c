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

static settings_t current = { .return_mode = RETURN_KEY_OPENED, .look = LOOK_PLAIN };
static char file_path[512];
static char dir_path[512];

static const char *const return_names[] = { "never", "key", "always" };
static const char *const look_names[] = { "plain", "picture" };
/* widgets=pomodoro,news のように、有効なものをカンマで並べる */
static const char *const widget_names[SETTINGS_WIDGETS] = { "none", "pomodoro", "news" };

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
        } else if (strcmp(key, "look") == 0) {
            for (int i = 0; i < 2; i++)
                if (strcmp(val, look_names[i]) == 0)
                    current.look = (look_t)i;
        } else if (strcmp(key, "widgets") == 0) {
            char list[128];
            snprintf(list, sizeof list, "%s", val);
            for (char *name = strtok(list, ","); name; name = strtok(NULL, ","))
                for (int i = 1; i < SETTINGS_WIDGETS; i++)
                    if (strcmp(name, widget_names[i]) == 0)
                        current.widgets[i] = true;
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
    fprintf(f, "look=%s\n", look_names[current.look]);
    fprintf(f, "widgets=");
    bool first = true;
    for (int i = 1; i < SETTINGS_WIDGETS; i++) {
        if (current.widgets[i]) {
            fprintf(f, "%s%s", first ? "" : ",", widget_names[i]);
            first = false;
        }
    }
    fprintf(f, "\n");
    fclose(f);
}

settings_t *settings(void)
{
    return &current;
}

const char *settings_dir(void)
{
    if (!dir_path[0]) {
        /* --config で渡したファイルのあるフォルダ */
        snprintf(dir_path, sizeof dir_path, "%s", file_path);
        char *sep = strrchr(dir_path, SEP[0]);
#if defined(_WIN32)
        char *slash = strrchr(dir_path, '/');
        if (slash && (!sep || slash > sep))
            sep = slash;
#endif
        if (sep)
            *sep = '\0';
        else
            snprintf(dir_path, sizeof dir_path, ".");
    }
    return dir_path;
}
