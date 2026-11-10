#include "platform.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>

void platform_audio_play(const char *wav_path)
{
    if (GetFileAttributesA(wav_path) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "audio: no %s\n", wav_path);
        return;
    }
    if (!PlaySoundA(wav_path, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT))
        fprintf(stderr, "audio: cannot play %s\n", wav_path);
}

void platform_audio_stop(void)
{
    PlaySoundA(NULL, NULL, 0);
}

uint32_t platform_now_ms(void)
{
    return (uint32_t)GetTickCount64();
}

void platform_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

void platform_localtime(time_t t, struct tm *out)
{
    localtime_s(out, &t);
}

bool platform_run_tool(const char *script, const char *args)
{
    /* exe（build-windows\skjar-ui.exe）のフォルダの 1 つ上の tools\ */
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof dir);
    if (n == 0 || n >= sizeof dir)
        return false;
    char *sep = strrchr(dir, '\\');
    if (!sep)
        return false;
    *sep = '\0';
    char tool[MAX_PATH + 64];
    snprintf(tool, sizeof tool, "%s\\..\\tools\\%s", dir, script);
    if (GetFileAttributesA(tool) == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "tool: no %s\n", tool);
        return false;
    }
    char cmd[2048];
    snprintf(cmd, sizeof cmd, "python \"%s\" %s", tool, args ? args : "");
    STARTUPINFOA si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    /* 端末を受け継ぐ（道具の出力が UI と同じ端末に出る） */
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "tool: cannot run python (error %lu)\n", (unsigned long)GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    fprintf(stderr, "tool: started %s\n", script);
    return true;
}

/* 標準入力は待たずに読めないので、別のスレッドで 1 行ずつ読んで、受け渡しの箱に入れる */
#define STDIN_SLOTS 16
#define STDIN_LEN   128
static char stdin_lines[STDIN_SLOTS][STDIN_LEN];
static int stdin_head, stdin_count;
static CRITICAL_SECTION stdin_lock;
static bool stdin_started;

static DWORD WINAPI stdin_thread(LPVOID arg)
{
    (void)arg;
    char line[STDIN_LEN];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        EnterCriticalSection(&stdin_lock);
        if (stdin_count < STDIN_SLOTS) {
            snprintf(stdin_lines[(stdin_head + stdin_count) % STDIN_SLOTS], STDIN_LEN, "%s", line);
            stdin_count++;
        }
        LeaveCriticalSection(&stdin_lock);
    }
    return 0;
}

bool platform_read_stdin_line(char *buf, size_t size)
{
    if (!stdin_started) {
        InitializeCriticalSection(&stdin_lock);
        CreateThread(NULL, 0, stdin_thread, NULL, 0, NULL);
        stdin_started = true;
    }
    bool got = false;
    EnterCriticalSection(&stdin_lock);
    if (stdin_count > 0) {
        snprintf(buf, size, "%s", stdin_lines[stdin_head]);
        stdin_head = (stdin_head + 1) % STDIN_SLOTS;
        stdin_count--;
        got = true;
    }
    LeaveCriticalSection(&stdin_lock);
    return got;
}

#else

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t audio_pid = -1;

/* aplay（Raspberry Pi OS に入っている）を子のプロセスで動かす */
void platform_audio_play(const char *wav_path)
{
    if (access(wav_path, R_OK) != 0) {
        fprintf(stderr, "audio: no %s\n", wav_path);
        return;
    }
    platform_audio_stop();
    pid_t pid = fork();
    if (pid == 0) {
        execlp("aplay", "aplay", "-q", wav_path, (char *)NULL);
        _exit(127);
    }
    audio_pid = pid;
}

void platform_audio_stop(void)
{
    if (audio_pid > 0) {
        kill(audio_pid, SIGTERM);
        waitpid(audio_pid, NULL, 0);
        audio_pid = -1;
    }
}

uint32_t platform_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void platform_sleep_ms(uint32_t ms)
{
    usleep(ms * 1000);
}

void platform_localtime(time_t t, struct tm *out)
{
    localtime_r(&t, out);
}

bool platform_read_stdin_line(char *buf, size_t size)
{
    static char acc[128];
    static size_t len;
    struct pollfd p = { .fd = 0, .events = POLLIN };
    while (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
        char c;
        if (read(0, &c, 1) != 1)
            return false;
        if (c == '\r')
            continue;
        if (c == '\n') {
            acc[len] = '\0';
            snprintf(buf, size, "%s", acc);
            len = 0;
            return true;
        }
        if (len < sizeof acc - 1)
            acc[len++] = c;
    }
    return false;
}

bool platform_run_tool(const char *script, const char *args)
{
    (void)script;
    (void)args;
    return false;
}

#endif
