/*
 * ppv - platform layer for POSIX systems (Linux, BSD, macOS, ...).
 *
 * Linux: zero-copy transfer with splice() through an intermediate pipe,
 *        process I/O counters from /proc/<pid>/io.
 * Other: read()/write(); --pid is not supported.
 */
/* Feature test macros: glibc hides splice()/pipe2() without _GNU_SOURCE. The
 * BSDs and macOS expose their full namespace by default, so nothing is
 * requested there (a strict _POSIX_C_SOURCE would hide TIOCGWINSZ and
 * pthread_cond_timedwait_relative_np). */
#if defined(__linux__)
#  define _GNU_SOURCE
#elif defined(__APPLE__)
#  define _DARWIN_C_SOURCE
#endif

#include "platform/platform.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------ */
/* Process setup, interruption, time                                        */

static volatile sig_atomic_t g_interrupted; /* the signal number, 0 = none */

/* Standard descriptors that were closed at start-up (bit n = fd n). */
static int g_closed_std_fds;

static void on_signal(int sig)
{
    g_interrupted = sig;
}

/* Reopen closed standard descriptors on /dev/null. Without this the pipe or
 * file opened later would get number 0, 1 or 2 and silently become our
 * stdin/stdout/stderr (ppv would e.g. splice from its own pipe forever). */
static int ensure_std_fds(void)
{
    for (int fd = 0; fd <= 2; fd++) {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF)
            continue;
        g_closed_std_fds |= 1 << fd;
        int nfd = open("/dev/null", fd == 0 ? O_RDONLY : O_WRONLY);
        if (nfd < 0)
            return -1;
        if (nfd != fd) { /* cannot happen (fd is the lowest free number), but be safe */
            if (dup2(nfd, fd) < 0)
                return -1;
            close(nfd);
        }
    }
    return 0;
}

int ppv_platform_init(void)
{
    struct sigaction sa;

    if (ensure_std_fds() != 0)
        return -1;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);

    /* A closed reader must show up as EPIPE, not kill us silently. */
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0)
        return -1;

    sa.sa_handler = on_signal;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0)
        return -1;
    return 0;
}

bool ppv_interrupted(void)
{
    return g_interrupted != 0;
}

int ppv_interrupt_exit_code(void)
{
    return 128 + (int)g_interrupted;
}

void ppv_exit_interrupted(void)
{
    int sig = (int)g_interrupted;
    fflush(NULL);
    if (sig > 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = SIG_DFL;
        if (sigaction(sig, &sa, NULL) == 0) {
            sigset_t set;
            sigemptyset(&set);
            sigaddset(&set, sig);
            pthread_sigmask(SIG_UNBLOCK, &set, NULL);
            PPV_COVERAGE_FLUSH(); /* as late as possible: nothing after this is recorded */
            raise(sig);
        }
    }
    PPV_COVERAGE_FLUSH();
    _Exit(ppv_interrupt_exit_code()); /* re-raise failed or the signal is unknown */
}

uint64_t ppv_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * PPV_NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

void ppv_sleep_ns(uint64_t ns)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ns / PPV_NS_PER_SEC);
    ts.tv_nsec = (long)(ns % PPV_NS_PER_SEC);
    (void)nanosleep(&ts, NULL); /* an interrupting signal ends the sleep early */
}

/* ------------------------------------------------------------------------ */
/* Status output                                                            */

bool ppv_stderr_is_tty(void)
{
    return isatty(STDERR_FILENO) == 1;
}

bool ppv_stderr_in_foreground(void)
{
    /* Job control only exists on a terminal. Ask only then: FreeBSD answers
     * TIOCGPGRP on a pipe too (the SIGIO owner, 0 if none), which would make
     * every ppv -f writing into a pipe look like a background job. */
    if (isatty(STDERR_FILENO) != 1)
        return true;
    pid_t fg = tcgetpgrp(STDERR_FILENO);
    return fg == -1 || fg == getpgrp();
}

unsigned ppv_stderr_columns(void)
{
    struct winsize ws;
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != 0)
        return 0;
    return ws.ws_col;
}

void ppv_stderr_write(const char *s, size_t len)
{
    while (len > 0) {
        ssize_t n = write(STDERR_FILENO, s, len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        s += n;
        len -= (size_t)n;
    }
}

/* ------------------------------------------------------------------------ */
/* Data stream                                                              */

struct ppv_stream {
    unsigned char *buf;
    size_t cap;
    size_t staged;       /* bytes read but not yet written */
    bool staged_in_pipe; /* staged bytes are in the splice pipe, not buf */
    int err;
#if defined(__linux__)
    bool use_splice;
    int mid[2]; /* intermediate pipe for splice in -> mid -> out */
    size_t pipe_cap;
#endif
};

/* Wait until fd is ready; needed only if a caller left it O_NONBLOCK. */
static void wait_fd(int fd, short events)
{
    struct pollfd p;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    while (poll(&p, 1, -1) < 0 && errno == EINTR) {}
}

static ppv_io_status fail(ppv_stream *s, int err)
{
    s->err = err;
    return err == EPIPE ? PPV_IO_BROKEN : PPV_IO_ERROR;
}

static ppv_io_status write_all(ppv_stream *s, const unsigned char *p, size_t len)
{
    while (len > 0) {
        ssize_t n = write(STDOUT_FILENO, p, len);
        if (n > 0) {
            p += n;
            len -= (size_t)n;
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            wait_fd(STDOUT_FILENO, POLLOUT);
        } else {
            return fail(s, n < 0 ? errno : EIO);
        }
    }
    return PPV_IO_OK;
}

ppv_stream *ppv_stream_open(size_t buffer_size, char *err, size_t err_cap)
{
    if (g_closed_std_fds & (1 << STDIN_FILENO)) {
        snprintf(err, err_cap, "standard input is closed");
        return NULL;
    }
    if (g_closed_std_fds & (1 << STDOUT_FILENO)) {
        snprintf(err, err_cap, "standard output is closed");
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
    s->cap = buffer_size;

#if defined(__linux__)
    s->mid[0] = s->mid[1] = -1;
    if (pipe2(s->mid, O_CLOEXEC) == 0) {
        /* Grow the pipe towards the buffer size (limited by pipe-max-size). */
        size_t want = buffer_size;
        while (want >= 65536 && fcntl(s->mid[1], F_SETPIPE_SZ, (int)want) < 0)
            want /= 2;
        int got = fcntl(s->mid[1], F_GETPIPE_SZ);
        s->pipe_cap = got > 0 ? (size_t)got : 65536;
        s->use_splice = true;
    }
#endif
    return s;
}

ppv_io_status ppv_stream_fill(ppv_stream *s, size_t *got)
{
    *got = 0;

#if defined(__linux__)
    while (s->use_splice) {
        ssize_t n = splice(STDIN_FILENO, NULL, s->mid[1], NULL, s->pipe_cap, SPLICE_F_MOVE);
        if (n > 0) {
            s->staged = (size_t)n;
            s->staged_in_pipe = true;
            *got = (size_t)n;
            return PPV_IO_OK;
        }
        if (n == 0)
            return PPV_IO_EOF;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN) {
            wait_fd(STDIN_FILENO, POLLIN);
            continue;
        }
        if (errno == EINVAL || errno == ENOSYS) {
            s->use_splice = false; /* stdin cannot splice (e.g. a terminal) */
            break;
        }
        return fail(s, errno);
    }
#endif

    for (;;) {
        ssize_t n = read(STDIN_FILENO, s->buf, s->cap);
        if (n > 0) {
            s->staged = (size_t)n;
            s->staged_in_pipe = false;
            *got = (size_t)n;
            return PPV_IO_OK;
        }
        if (n == 0)
            return PPV_IO_EOF;
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            wait_fd(STDIN_FILENO, POLLIN);
            continue;
        }
        return fail(s, errno);
    }
}

ppv_io_status ppv_stream_drain(ppv_stream *s)
{
#if defined(__linux__)
    if (s->staged_in_pipe) {
        while (s->staged > 0) {
            if (s->use_splice) {
                ssize_t n = splice(s->mid[0], NULL, STDOUT_FILENO, NULL, s->staged, SPLICE_F_MOVE);
                if (n > 0) {
                    s->staged -= (size_t)n;
                } else if (n < 0 && errno == EINTR) {
                    continue;
                } else if (n < 0 && errno == EAGAIN) {
                    wait_fd(STDOUT_FILENO, POLLOUT);
                } else if (n < 0 && (errno == EINVAL || errno == ENOSYS)) {
                    s->use_splice = false; /* stdout cannot splice (e.g. O_APPEND, tty) */
                } else {
                    return fail(s, n < 0 ? errno : EIO);
                }
                continue;
            }
            /* Fallback: move what is left in the pipe the classic way. */
            size_t want = s->staged < s->cap ? s->staged : s->cap;
            ssize_t n = read(s->mid[0], s->buf, want);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return fail(s, n < 0 ? errno : EIO);
            ppv_io_status st = write_all(s, s->buf, (size_t)n);
            if (st != PPV_IO_OK)
                return st;
            s->staged -= (size_t)n;
        }
        s->staged_in_pipe = false;
        return PPV_IO_OK;
    }
#endif

    ppv_io_status st = write_all(s, s->buf, s->staged);
    s->staged = 0;
    return st;
}

const char *ppv_stream_method(const ppv_stream *s)
{
#if defined(__linux__)
    if (s->use_splice)
        return "splice";
#else
    (void)s;
#endif
    return "read/write";
}

/* strerror() is not thread-safe; this runs in the transfer thread. */
static void errno_text(int err, char *buf, size_t cap)
{
    if (cap == 0)
        return;
    buf[0] = '\0';
#if defined(__GLIBC__) && defined(_GNU_SOURCE)
    /* glibc with _GNU_SOURCE provides the GNU variant, which may return a
     * static string instead of filling buf. */
    const char *msg = strerror_r(err, buf, cap);
    if (msg != buf)
        snprintf(buf, cap, "%s", msg);
#else
    if (strerror_r(err, buf, cap) != 0)
        snprintf(buf, cap, "error %d", err);
#endif
}

void ppv_stream_error(const ppv_stream *s, char *buf, size_t cap)
{
    errno_text(s->err, buf, cap);
}

void ppv_stream_close(ppv_stream *s)
{
    if (s == NULL)
        return;
#if defined(__linux__)
    if (s->mid[0] >= 0)
        close(s->mid[0]);
    if (s->mid[1] >= 0)
        close(s->mid[1]);
#endif
    free(s->buf);
    free(s);
}

/* ------------------------------------------------------------------------ */
/* Threads and events                                                       */

struct ppv_thread {
    pthread_t id;
    void (*fn)(void *);
    void *arg;
};

static void *thread_entry(void *p)
{
    ppv_thread *t = p;
    t->fn(t->arg);
    return NULL;
}

ppv_thread *ppv_thread_start(void (*fn)(void *), void *arg)
{
    ppv_thread *t = malloc(sizeof *t);
    if (t == NULL)
        return NULL;
    t->fn = fn;
    t->arg = arg;
    if (pthread_create(&t->id, NULL, thread_entry, t) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

void ppv_thread_join(ppv_thread *t)
{
    if (t == NULL)
        return;
    pthread_join(t->id, NULL);
    free(t);
}

struct ppv_event {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    bool set;
};

ppv_event *ppv_event_create(void)
{
    ppv_event *e = calloc(1, sizeof *e);
    if (e == NULL)
        return NULL;

    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
#if !defined(__APPLE__)
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
#endif
    if (pthread_mutex_init(&e->mu, NULL) != 0 || pthread_cond_init(&e->cv, &ca) != 0) {
        pthread_condattr_destroy(&ca);
        free(e);
        return NULL;
    }
    pthread_condattr_destroy(&ca);
    return e;
}

void ppv_event_set(ppv_event *e)
{
    pthread_mutex_lock(&e->mu);
    e->set = true;
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mu);
}

bool ppv_event_wait(ppv_event *e, uint64_t timeout_ns)
{
    pthread_mutex_lock(&e->mu);
    if (!e->set && timeout_ns > 0) {
        struct timespec ts;
#if defined(__APPLE__)
        ts.tv_sec = (time_t)(timeout_ns / PPV_NS_PER_SEC);
        ts.tv_nsec = (long)(timeout_ns % PPV_NS_PER_SEC);
        (void)pthread_cond_timedwait_relative_np(&e->cv, &e->mu, &ts);
#else
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t nsec = (uint64_t)ts.tv_nsec + timeout_ns % PPV_NS_PER_SEC;
        ts.tv_sec += (time_t)(timeout_ns / PPV_NS_PER_SEC + nsec / PPV_NS_PER_SEC);
        ts.tv_nsec = (long)(nsec % PPV_NS_PER_SEC);
        (void)pthread_cond_timedwait(&e->cv, &e->mu, &ts);
#endif
    }
    bool set = e->set;
    pthread_mutex_unlock(&e->mu);
    return set;
}

void ppv_event_destroy(ppv_event *e)
{
    if (e == NULL)
        return;
    pthread_cond_destroy(&e->cv);
    pthread_mutex_destroy(&e->mu);
    free(e);
}

/* ------------------------------------------------------------------------ */
/* Process I/O counters                                                     */

#if defined(__linux__)

struct ppv_proc {
    pid_t pid;
    int io_fd;
};

ppv_proc *ppv_proc_open(uint64_t pid, char *err, size_t err_cap)
{
    if (pid == 0 || pid > INT32_MAX) {
        snprintf(err, err_cap, "invalid process id");
        return NULL;
    }

    char path[64];
    snprintf(path, sizeof path, "/proc/%d/io", (int)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        char dir[64];
        snprintf(dir, sizeof dir, "/proc/%d", (int)pid);
        if (e == ENOENT && access(dir, F_OK) == 0)
            snprintf(err, err_cap, "kernel without I/O accounting (%s missing, e.g. WSL2)", path);
        else if (e == ENOENT)
            snprintf(err, err_cap, "no such process");
        else if (e == EACCES || e == EPERM)
            snprintf(err, err_cap, "permission denied (process of another user?)");
        else
            errno_text(e, err, err_cap);
        return NULL;
    }

    ppv_proc *p = malloc(sizeof *p);
    if (p == NULL) {
        close(fd);
        snprintf(err, err_cap, "out of memory");
        return NULL;
    }
    p->pid = (pid_t)pid;
    p->io_fd = fd;
    return p;
}

static bool parse_field(const char *text, const char *key, uint64_t *out)
{
    const char *p = strstr(text, key);
    if (p == NULL)
        return false;
    char *end;
    unsigned long long v = strtoull(p + strlen(key), &end, 10);
    if (end == p + strlen(key))
        return false;
    *out = (uint64_t)v;
    return true;
}

bool ppv_proc_io(ppv_proc *p, uint64_t *read_bytes, uint64_t *write_bytes)
{
    char buf[1024];
    if (lseek(p->io_fd, 0, SEEK_SET) < 0)
        return false;
    ssize_t n = read(p->io_fd, buf, sizeof buf - 1);
    if (n <= 0)
        return false;
    buf[n] = '\0';
    /* rchar/wchar count all I/O incl. pipes and sockets, like Windows does. */
    return parse_field(buf, "rchar:", read_bytes) && parse_field(buf, "wchar:", write_bytes);
}

static bool proc_gone(pid_t pid)
{
    if (kill(pid, 0) != 0 && errno == ESRCH)
        return true;

    /* A zombie still exists for kill(); check its state in /proc/<pid>/stat. */
    char path[64], buf[512];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return true;
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return true;
    buf[n] = '\0';
    const char *rp = strrchr(buf, ')');
    return rp != NULL && rp[1] == ' ' && (rp[2] == 'Z' || rp[2] == 'X');
}

bool ppv_proc_wait_exit(ppv_proc *p, uint64_t timeout_ns)
{
    ppv_sleep_ns(timeout_ns);
    return proc_gone(p->pid);
}

void ppv_proc_close(ppv_proc *p)
{
    if (p == NULL)
        return;
    close(p->io_fd);
    free(p);
}

#else /* !__linux__ */

struct ppv_proc {
    int unused;
};

ppv_proc *ppv_proc_open(uint64_t pid, char *err, size_t err_cap)
{
    (void)pid;
    snprintf(err, err_cap, "watching a process is not supported on this platform");
    return NULL;
}

bool ppv_proc_io(ppv_proc *p, uint64_t *read_bytes, uint64_t *write_bytes)
{
    (void)p;
    (void)read_bytes;
    (void)write_bytes;
    return false;
}

bool ppv_proc_wait_exit(ppv_proc *p, uint64_t timeout_ns)
{
    (void)p;
    (void)timeout_ns;
    return true;
}

void ppv_proc_close(ppv_proc *p)
{
    (void)p;
}

#endif
