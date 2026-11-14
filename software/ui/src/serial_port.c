#include "serial_port.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

static HANDLE port = INVALID_HANDLE_VALUE;

bool serial_open(const char *path)
{
    /* COM10 以上も開けるよう、\\.\COMn の形にする */
    char name[64];
    if (strncmp(path, "\\\\.\\", 4) == 0)
        snprintf(name, sizeof name, "%s", path);
    else
        snprintf(name, sizeof name, "\\\\.\\%s", path);

    port = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (port == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "serial: cannot open %s (error %lu)\n", path, GetLastError());
        return false;
    }

    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if (!GetCommState(port, &dcb)) {
        fprintf(stderr, "serial: GetCommState failed (error %lu)\n", GetLastError());
        serial_close();
        return false;
    }
    dcb.BaudRate = 115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    if (!SetCommState(port, &dcb)) {
        fprintf(stderr, "serial: SetCommState failed (error %lu)\n", GetLastError());
        serial_close();
        return false;
    }

    /* 読み出しは、届いている分だけ返してすぐ戻る */
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout = MAXDWORD;
    to.WriteTotalTimeoutConstant = 100;
    SetCommTimeouts(port, &to);
    return true;
}

bool serial_is_open(void)
{
    return port != INVALID_HANDLE_VALUE;
}

int serial_read(char *buf, size_t len)
{
    DWORD n = 0;
    if (!ReadFile(port, buf, (DWORD)len, &n, NULL))
        return -1;
    return (int)n;
}

int serial_write(const char *buf, size_t len)
{
    DWORD n = 0;
    if (!WriteFile(port, buf, (DWORD)len, &n, NULL))
        return -1;
    return (int)n;
}

void serial_close(void)
{
    if (port != INVALID_HANDLE_VALUE)
        CloseHandle(port);
    port = INVALID_HANDLE_VALUE;
}

#else

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static int fd = -1;

static bool configure_tty(int tty_fd)
{
    struct termios t;
    if (tcgetattr(tty_fd, &t) != 0)
        return false;
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(CSTOPB | CRTSCTS);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    return tcsetattr(tty_fd, TCSANOW, &t) == 0;
}

bool serial_open(const char *path)
{
    fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        fprintf(stderr, "serial: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (isatty(fd) && !configure_tty(fd))
        fprintf(stderr, "serial: cannot configure %s: %s\n", path, strerror(errno));
    return true;
}

bool serial_is_open(void)
{
    return fd >= 0;
}

int serial_read(char *buf, size_t len)
{
    ssize_t r = read(fd, buf, len);
    if (r >= 0)
        return (int)r;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    return -1;
}

int serial_write(const char *buf, size_t len)
{
    ssize_t w = write(fd, buf, len);
    return w < 0 ? -1 : (int)w;
}

void serial_close(void)
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}

#endif
