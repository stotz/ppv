/*
 * ppv - platform layer for Windows (Windows 7 and later).
 *
 * Data is moved with ReadFile()/WriteFile() on the raw standard handles, so
 * there is no C runtime text mode and no CR/LF translation.
 */
#include "platform/platform.h"

#include <windows.h>

#include <process.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ------------------------------------------------------------------------ */
/* Process setup, interruption, time                                        */

static volatile LONG g_interrupted;
static LARGE_INTEGER g_qpc_freq;

static BOOL WINAPI on_console_ctrl(DWORD type)
{
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
        InterlockedExchange(&g_interrupted, 1);
        return TRUE; /* the main loop reports and exits */
    default:
        return FALSE;
    }
}

int ppv_platform_init(void)
{
    if (!QueryPerformanceFrequency(&g_qpc_freq) || g_qpc_freq.QuadPart <= 0)
        return -1;
    (void)SetConsoleCtrlHandler(on_console_ctrl, TRUE);
    return 0;
}

bool ppv_interrupted(void)
{
    return InterlockedCompareExchange(&g_interrupted, 0, 0) != 0;
}

int ppv_interrupt_exit_code(void)
{
    return 130; /* 128 + SIGINT, as on POSIX */
}

void ppv_exit_interrupted(void)
{
    fflush(NULL);
    _Exit(ppv_interrupt_exit_code());
}

uint64_t ppv_now_ns(void)
{
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    uint64_t t = (uint64_t)c.QuadPart;
    uint64_t f = (uint64_t)g_qpc_freq.QuadPart;
    return (t / f) * PPV_NS_PER_SEC + (t % f) * PPV_NS_PER_SEC / f;
}

static DWORD ns_to_ms(uint64_t ns)
{
    uint64_t ms = (ns + 999999) / 1000000;
    return ms >= INFINITE ? INFINITE - 1 : (DWORD)ms;
}

void ppv_sleep_ns(uint64_t ns)
{
    Sleep(ns_to_ms(ns));
}

/* ------------------------------------------------------------------------ */
/* Status output                                                            */

/* mintty (git-bash, MSYS2, Cygwin) connects programs to named pipes such as
 * \msys-1888ae32e00d56aa-pty0-to-master instead of a console. */
static bool is_cygwin_pty(HANDLE h)
{
    if (GetFileType(h) != FILE_TYPE_PIPE)
        return false;

    union {
        FILE_NAME_INFO info;
        unsigned char raw[sizeof(FILE_NAME_INFO) + MAX_PATH * sizeof(WCHAR)];
    } u;
    if (!GetFileInformationByHandleEx(h, FileNameInfo, &u, sizeof u))
        return false;

    WCHAR name[MAX_PATH + 1];
    size_t n = u.info.FileNameLength / sizeof(WCHAR);
    if (n > MAX_PATH)
        n = MAX_PATH;
    memcpy(name, u.raw + offsetof(FILE_NAME_INFO, FileName), n * sizeof(WCHAR));
    name[n] = L'\0';

    return (wcsstr(name, L"msys-") != NULL || wcsstr(name, L"cygwin-") != NULL) &&
           wcsstr(name, L"-pty") != NULL &&
           (wcsstr(name, L"-to-master") != NULL || wcsstr(name, L"-from-master") != NULL);
}

bool ppv_stderr_is_tty(void)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return false;
    DWORD mode;
    if (GetConsoleMode(h, &mode))
        return true;
    return is_cygwin_pty(h);
}

bool ppv_stderr_in_foreground(void)
{
    return true; /* no job control on Windows */
}

unsigned ppv_stderr_columns(void)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (h == NULL || h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &info))
        return 0; /* not a console (e.g. a mintty pty): unknown */
    int cols = info.srWindow.Right - info.srWindow.Left + 1;
    return cols > 0 ? (unsigned)cols : 0;
}

void ppv_stderr_write(const char *s, size_t len)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h == NULL || h == INVALID_HANDLE_VALUE)
        return;
    while (len > 0) {
        DWORD n = 0;
        DWORD chunk = len > 32768 ? 32768 : (DWORD)len;
        if (!WriteFile(h, s, chunk, &n, NULL) || n == 0)
            return;
        s += n;
        len -= n;
    }
}

/* ------------------------------------------------------------------------ */
/* Data stream                                                              */

struct ppv_stream {
    HANDLE in;
    HANDLE out;
    bool in_is_pipe;
    size_t out_chunk; /* console writes must stay small on old Windows */
    unsigned char *buf;
    size_t cap;
    size_t staged;
    DWORD err;
};

static bool valid_handle(HANDLE h)
{
    return h != NULL && h != INVALID_HANDLE_VALUE;
}

ppv_stream *ppv_stream_open(size_t buffer_size, char *err, size_t err_cap)
{
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!valid_handle(in) || !valid_handle(out)) {
        snprintf(err, err_cap, "standard input or output is not available");
        return NULL;
    }

    ppv_stream *s = calloc(1, sizeof *s);
    if (s != NULL)
        s->buf = malloc(buffer_size);
    if (s == NULL || s->buf == NULL) {
        free(s);
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    s->in = in;
    s->out = out;
    s->cap = buffer_size;
    s->in_is_pipe = GetFileType(in) == FILE_TYPE_PIPE;
    s->out_chunk = GetFileType(out) == FILE_TYPE_CHAR ? 32768 : buffer_size;
    return s;
}

ppv_io_status ppv_stream_fill(ppv_stream *s, size_t *got)
{
    *got = 0;
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(s->in, s->buf, (DWORD)s->cap, &n, NULL)) {
            DWORD e = GetLastError();
            if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF)
                return PPV_IO_EOF; /* writer closed its end */
            s->err = e;
            return PPV_IO_ERROR;
        }
        if (n == 0) {
            /* On a pipe a zero-length read means the writer wrote 0 bytes;
             * end of data is signalled with ERROR_BROKEN_PIPE instead. */
            if (s->in_is_pipe)
                continue;
            return PPV_IO_EOF;
        }
        s->staged = n;
        *got = n;
        return PPV_IO_OK;
    }
}

ppv_io_status ppv_stream_drain(ppv_stream *s)
{
    const unsigned char *p = s->buf;
    while (s->staged > 0) {
        size_t chunk = s->staged < s->out_chunk ? s->staged : s->out_chunk;
        DWORD n = 0;
        if (!WriteFile(s->out, p, (DWORD)chunk, &n, NULL)) {
            s->err = GetLastError();
            s->staged = 0;
            return s->err == ERROR_BROKEN_PIPE || s->err == ERROR_NO_DATA ? PPV_IO_BROKEN
                                                                          : PPV_IO_ERROR;
        }
        p += n;
        s->staged -= n;
    }
    return PPV_IO_OK;
}

const char *ppv_stream_method(const ppv_stream *s)
{
    (void)s;
    return "ReadFile/WriteFile";
}

static void format_error(DWORD code, char *buf, size_t cap)
{
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
                             0, buf, (DWORD)cap, NULL);
    if (n == 0) {
        snprintf(buf, cap, "error %lu", (unsigned long)code);
        return;
    }
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == '.'))
        buf[--n] = '\0';
}

void ppv_stream_error(const ppv_stream *s, char *buf, size_t cap)
{
    format_error(s->err, buf, cap);
}

void ppv_stream_close(ppv_stream *s)
{
    if (s == NULL)
        return;
    free(s->buf);
    free(s);
}

/* ------------------------------------------------------------------------ */
/* Threads and events                                                       */

struct ppv_thread {
    HANDLE h;
    void (*fn)(void *);
    void *arg;
};

static unsigned __stdcall thread_entry(void *p)
{
    ppv_thread *t = p;
    t->fn(t->arg);
    return 0;
}

ppv_thread *ppv_thread_start(void (*fn)(void *), void *arg)
{
    ppv_thread *t = malloc(sizeof *t);
    if (t == NULL)
        return NULL;
    t->fn = fn;
    t->arg = arg;
    uintptr_t h = _beginthreadex(NULL, 0, thread_entry, t, 0, NULL);
    if (h == 0) {
        free(t);
        return NULL;
    }
    t->h = (HANDLE)h;
    return t;
}

void ppv_thread_join(ppv_thread *t)
{
    if (t == NULL)
        return;
    WaitForSingleObject(t->h, INFINITE);
    CloseHandle(t->h);
    free(t);
}

struct ppv_event {
    HANDLE h;
};

ppv_event *ppv_event_create(void)
{
    ppv_event *e = malloc(sizeof *e);
    if (e == NULL)
        return NULL;
    e->h = CreateEventW(NULL, TRUE, FALSE, NULL); /* manual reset = latched */
    if (e->h == NULL) {
        free(e);
        return NULL;
    }
    return e;
}

void ppv_event_set(ppv_event *e)
{
    SetEvent(e->h);
}

bool ppv_event_wait(ppv_event *e, uint64_t timeout_ns)
{
    return WaitForSingleObject(e->h, ns_to_ms(timeout_ns)) == WAIT_OBJECT_0;
}

void ppv_event_destroy(ppv_event *e)
{
    if (e == NULL)
        return;
    CloseHandle(e->h);
    free(e);
}

/* ------------------------------------------------------------------------ */
/* Process I/O counters                                                     */

struct ppv_proc {
    HANDLE h;
};

ppv_proc *ppv_proc_open(uint64_t pid, char *err, size_t err_cap)
{
    if (pid == 0 || pid > UINT32_MAX) {
        snprintf(err, err_cap, "invalid process id");
        return NULL;
    }

    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, (DWORD)pid);
    if (h == NULL) {
        DWORD e = GetLastError();
        if (e == ERROR_INVALID_PARAMETER)
            snprintf(
                err, err_cap,
                "no such process (in git-bash use the Windows PID: ps -W or /proc/PID/winpid)");
        else if (e == ERROR_ACCESS_DENIED)
            snprintf(err, err_cap, "permission denied");
        else
            format_error(e, err, err_cap);
        return NULL;
    }

    ppv_proc *p = malloc(sizeof *p);
    if (p == NULL) {
        CloseHandle(h);
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    p->h = h;
    return p;
}

bool ppv_proc_io(ppv_proc *p, uint64_t *read_bytes, uint64_t *write_bytes)
{
    IO_COUNTERS c;
    if (!GetProcessIoCounters(p->h, &c))
        return false;
    *read_bytes = c.ReadTransferCount;
    *write_bytes = c.WriteTransferCount;
    return true;
}

bool ppv_proc_wait_exit(ppv_proc *p, uint64_t timeout_ns)
{
    return WaitForSingleObject(p->h, ns_to_ms(timeout_ns)) == WAIT_OBJECT_0;
}

void ppv_proc_close(ppv_proc *p)
{
    if (p == NULL)
        return;
    CloseHandle(p->h);
    free(p);
}
