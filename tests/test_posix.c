/*
 * POSIX end-to-end tests that need control over descriptors and signals:
 *
 *   - closed standard descriptors (ppv must not hang or corrupt the stream)
 *   - Ctrl-C / SIGTERM: termination by the same signal, or 128 + signal
 *   - stdout opened with O_APPEND (Linux: splice falls back to read/write)
 *   - a read error (directory as stdin)
 *   - non-blocking pipes inherited from the parent
 *   - --pid error messages (Linux)
 *
 *   test_posix PPV GEN CHECK WORKDIR
 */
#if defined(__linux__)
#  define _GNU_SOURCE
#endif

#include "test.h"

#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SIZE "3000000"

static char *g_ppv, *g_gen, *g_check;
static const char *g_work;

static void sleep_ms(unsigned ms)
{
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    (void)nanosleep(&ts, NULL);
}

/*
 * Starts argv with the given descriptors on 0, 1, 2. A negative value closes
 * that descriptor in the child.
 */
static pid_t spawn(char **argv, int in, int out, int err)
{
    pid_t p = fork();
    if (p < 0) {
        perror("fork");
        exit(1);
    }
    if (p != 0)
        return p;

    int fds[3] = {in, out, err};
    /* Move the sources out of the way first so that dup2 cannot clobber one. */
    for (int i = 0; i < 3; i++)
        if (fds[i] >= 0 && fds[i] < 3)
            fds[i] = fcntl(fds[i], F_DUPFD, 3);
    for (int i = 0; i < 3; i++) {
        if (fds[i] < 0)
            close(i);
        else if (dup2(fds[i], i) < 0)
            _exit(127);
    }
    /* Nothing else (pipe ends held by the parent, ...) must leak into the child:
     * an inherited write end would keep a pipe from ever reporting EOF. */
    for (int fd = 3; fd < 1024; fd++)
        close(fd);
    execv(argv[0], argv);
    _exit(127);
}

/* Waits up to timeout_ms; kills the child and returns false on timeout. */
static bool wait_for(pid_t p, unsigned timeout_ms, int *status)
{
    for (unsigned waited = 0;; waited += 10) {
        pid_t r = waitpid(p, status, WNOHANG);
        if (r == p)
            return true;
        if (r < 0 && errno != EINTR) {
            perror("waitpid");
            return false;
        }
        if (waited >= timeout_ms) {
            kill(p, SIGKILL);
            waitpid(p, status, 0);
            return false;
        }
        sleep_ms(10);
    }
}

static int exit_code(int status)
{
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Reads everything from fd (which must deliver EOF) into buf. */
static void read_all(int fd, char *buf, size_t cap)
{
    size_t len = 0;
    for (;;) {
        ssize_t n = read(fd, buf + len, cap - 1 - len);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || (size_t)n == cap - 1 - len)
            break;
        len += (size_t)n;
    }
    buf[len] = '\0';
}

/* pipe() that cannot fail silently (the analyzer would otherwise see the
 * descriptors as possibly uninitialized). */
static void make_pipe(int fds[2])
{
    if (pipe(fds) != 0) {
        perror("pipe");
        exit(1);
    }
}

static int open_null(int flags)
{
    int fd = open("/dev/null", flags);
    if (fd < 0) {
        perror("/dev/null");
        exit(1);
    }
    return fd;
}

/* --------------------------------------------------------------------- */

static void test_closed_stdin(void)
{
    static char q[] = "-q", s[] = "-s";
    char *argv[] = {g_ppv, q, s, NULL};
    int errp[2];
    make_pipe(errp);
    int null_out = open_null(O_WRONLY);
    pid_t p = spawn(argv, -1, null_out, errp[1]);
    close(errp[1]);
    close(null_out);

    /* Wait first (with a timeout, the old bug was a hang), then read the
     * message: ppv writes far less than a pipe buffers. */
    int status = 0;
    if (!wait_for(p, 5000, &status))
        TEST_FAIL("closed stdin: ppv did not exit (hang)");
    else
        EXPECT_EQ(exit_code(status), 1);
    static char text[4096];
    read_all(errp[0], text, sizeof text);
    close(errp[0]);
    EXPECT_CONTAINS(text, "standard input is closed");
    EXPECT_TRUE(strstr(text, "bytes)") == NULL); /* no statistics after a setup error */
}

static void test_closed_stdout(void)
{
    static char q[] = "-q";
    char *argv[] = {g_ppv, q, NULL};
    int errp[2];
    make_pipe(errp);
    int null_in = open_null(O_RDONLY);
    pid_t p = spawn(argv, null_in, -1, errp[1]);
    close(errp[1]);
    close(null_in);

    int status = 0;
    if (!wait_for(p, 5000, &status))
        TEST_FAIL("closed stdout: ppv did not exit (hang)");
    else
        EXPECT_EQ(exit_code(status), 1);
    static char text[4096];
    read_all(errp[0], text, sizeof text);
    close(errp[0]);
    EXPECT_CONTAINS(text, "standard output is closed");
}

/* gen | ppv -f (stderr closed) | check: the live line must not end up in the
 * data stream, and ppv must not hang. */
static void test_closed_stderr(void)
{
    static char size[] = SIZE, f[] = "-f", i[] = "-i", iv[] = "0.1";
    char *gen_argv[] = {g_gen, size, NULL};
    char *ppv_argv[] = {g_ppv, f, i, iv, NULL};
    char *check_argv[] = {g_check, size, NULL};

    int a[2], b[2];
    make_pipe(a);
    make_pipe(b);
    int null_in = open_null(O_RDONLY);
    pid_t gen = spawn(gen_argv, null_in, a[1], STDERR_FILENO);
    pid_t ppv = spawn(ppv_argv, a[0], b[1], -1);
    pid_t check = spawn(check_argv, b[0], STDOUT_FILENO, STDERR_FILENO);
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    close(null_in);

    int status = 0;
    if (!wait_for(ppv, 10000, &status))
        TEST_FAIL("closed stderr: ppv did not exit");
    else
        EXPECT_EQ(exit_code(status), 0);
    EXPECT_TRUE(wait_for(check, 10000, &status));
    EXPECT_EQ(exit_code(status), 0); /* data intact */
    EXPECT_TRUE(wait_for(gen, 10000, &status));
}

/* Reads from fd until `stop` has been seen or EOF; appends to buf. */
static void read_until(int fd, char stop, char *buf, size_t cap)
{
    size_t len = strlen(buf);
    while (len + 1 < cap) {
        ssize_t n = read(fd, buf + len, 1);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        len += (size_t)n;
        if (buf[len - 1] == stop)
            break;
    }
    buf[len] = '\0';
}

/* ppv -f -s blocked in read(); once the first live line has appeared the
 * signal handlers are certainly installed. The signal must end ppv promptly,
 * with the statistics marked as interrupted. */
static void test_signal(int sig)
{
    static char f[] = "-f", s[] = "-s", i[] = "-i", iv[] = "0.1";
    char *argv[] = {g_ppv, f, s, i, iv, NULL};
    int in[2], errp[2];
    make_pipe(in);
    make_pipe(errp);
    int null_out = open_null(O_WRONLY);
    pid_t p = spawn(argv, in[0], null_out, errp[1]);
    close(in[0]); /* we keep in[1] open: ppv blocks in read() */
    close(errp[1]);
    close(null_out);

    static char text[8192];
    text[0] = '\0';
    read_until(errp[0], '\r', text, sizeof text); /* first live line = sign of life */
    EXPECT_CONTAINS(text, "ppv[0B, avg 0B/s, p10 0B/s, p50 0B/s, p90 0B/s]");
    EXPECT_TRUE(kill(p, sig) == 0);

    int status = 0;
    if (!wait_for(p, 5000, &status)) {
        TEST_FAIL("signal %d: ppv did not exit", sig);
    } else if (WIFSIGNALED(status)) {
        EXPECT_EQ(WTERMSIG(status), sig);
    } else {
        EXPECT_EQ(exit_code(status), 128 + sig);
    }
    read_until(errp[0], '\0', text, sizeof text); /* the rest, up to EOF */
    close(errp[0]);
    EXPECT_CONTAINS(text, "]\n"); /* the final live line stays on screen */
    EXPECT_CONTAINS(text, "(interrupted)");
    EXPECT_CONTAINS(text, "(0 bytes)");
    close(in[1]);
}

/* ppv < file >> file: stdout is O_APPEND, which splice() refuses on Linux. */
static void test_append(void)
{
    char in_path[1024], out_path[1024];
    snprintf(in_path, sizeof in_path, "%s/test_posix_in.bin", g_work);
    snprintf(out_path, sizeof out_path, "%s/test_posix_out.bin", g_work);

    static char size[] = SIZE, q[] = "-q", s[] = "-s";
    char *gen_argv[] = {g_gen, size, NULL};
    char *ppv_argv[] = {g_ppv, q, s, NULL};
    char *check_argv[] = {g_check, size, NULL};
    int status = 0;

    int in_w = open(in_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    EXPECT_TRUE(in_w >= 0);
    int null_in = open_null(O_RDONLY);
    pid_t gen = spawn(gen_argv, null_in, in_w, STDERR_FILENO);
    close(in_w);
    EXPECT_TRUE(wait_for(gen, 10000, &status) && exit_code(status) == 0);

    int in_r = open(in_path, O_RDONLY);
    int out_a = open(out_path, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    int errp[2];
    EXPECT_TRUE(in_r >= 0 && out_a >= 0);
    make_pipe(errp);
    pid_t ppv = spawn(ppv_argv, in_r, out_a, errp[1]);
    close(in_r);
    close(out_a);
    close(errp[1]);
    EXPECT_TRUE(wait_for(ppv, 10000, &status));
    EXPECT_EQ(exit_code(status), 0);
    static char text[4096];
    read_all(errp[0], text, sizeof text);
    close(errp[0]);
    EXPECT_CONTAINS(text, "(3_000_000 bytes)");
#if defined(__linux__)
    EXPECT_CONTAINS(text, "read/write"); /* the splice fallback was taken */
#endif

    int out_r = open(out_path, O_RDONLY);
    EXPECT_TRUE(out_r >= 0);
    pid_t check = spawn(check_argv, out_r, STDOUT_FILENO, STDERR_FILENO);
    close(out_r);
    close(null_in);
    EXPECT_TRUE(wait_for(check, 10000, &status));
    EXPECT_EQ(exit_code(status), 0);

    unlink(in_path);
    unlink(out_path);
}

/* Reading from a directory fails (EISDIR): the read-error path of the
 * transfer, including the fallback from splice() on Linux. */
static void test_read_error(void)
{
    static char q[] = "-q", s[] = "-s";
    char *argv[] = {g_ppv, q, s, NULL};
    int dir = open("/", O_RDONLY);
    EXPECT_TRUE(dir >= 0);
    int errp[2];
    make_pipe(errp);
    int null_out = open_null(O_WRONLY);
    pid_t p = spawn(argv, dir, null_out, errp[1]);
    close(dir);
    close(errp[1]);
    close(null_out);

    int status = 0;
    if (!wait_for(p, 5000, &status))
        TEST_FAIL("read error: ppv did not exit");
    else
        EXPECT_EQ(exit_code(status), 1);
    static char text[4096];
    read_all(errp[0], text, sizeof text);
    close(errp[0]);
    EXPECT_CONTAINS(text, "ppv: read error: ");
}

/* Pipes inherited with O_NONBLOCK (as Node or asyncio parents hand them
 * out): ppv must wait with poll() instead of failing on EAGAIN, on both
 * sides. A slow producer guarantees empty-pipe reads. */
static void test_nonblocking(void)
{
    static char size[] = SIZE, delay[] = "2", q[] = "-q", s[] = "-s";
    char *gen_argv[] = {g_gen, size, delay, NULL};
    char *ppv_argv[] = {g_ppv, q, s, NULL};
    char *check_argv[] = {g_check, size, NULL};

    int a[2], b[2], errp[2];
    make_pipe(a);
    make_pipe(b);
    make_pipe(errp);
    /* The flag lives on the open file description, so setting it here makes
     * ppv's stdin and stdout non-blocking; gen's and check's ends are
     * separate descriptions and stay blocking. */
    EXPECT_TRUE(fcntl(a[0], F_SETFL, O_NONBLOCK) == 0);
    EXPECT_TRUE(fcntl(b[1], F_SETFL, O_NONBLOCK) == 0);
    int null_in = open_null(O_RDONLY);
    pid_t gen = spawn(gen_argv, null_in, a[1], STDERR_FILENO);
    pid_t ppv = spawn(ppv_argv, a[0], b[1], errp[1]);
    pid_t check = spawn(check_argv, b[0], STDOUT_FILENO, STDERR_FILENO);
    close(a[0]);
    close(a[1]);
    close(b[0]);
    close(b[1]);
    close(errp[1]);
    close(null_in);

    int status = 0;
    if (!wait_for(ppv, 20000, &status))
        TEST_FAIL("non-blocking: ppv did not exit");
    else
        EXPECT_EQ(exit_code(status), 0);
    static char text[4096];
    read_all(errp[0], text, sizeof text);
    close(errp[0]);
    EXPECT_CONTAINS(text, "(3_000_000 bytes)");
    EXPECT_TRUE(wait_for(check, 10000, &status));
    EXPECT_EQ(exit_code(status), 0); /* data intact */
    EXPECT_TRUE(wait_for(gen, 10000, &status));
}

#if defined(__linux__)
/* Runs ppv --pid PID and returns its exit code; stderr into text. */
static int run_pid(const char *pid_text, char *text, size_t cap)
{
    static char opt[] = "--pid";
    char arg[32];
    snprintf(arg, sizeof arg, "%s", pid_text);
    char *argv[] = {g_ppv, opt, arg, NULL};
    int errp[2];
    make_pipe(errp);
    int null_in = open_null(O_RDONLY), null_out = open_null(O_WRONLY);
    pid_t p = spawn(argv, null_in, null_out, errp[1]);
    close(null_in);
    close(null_out);
    close(errp[1]);
    int status = 0;
    if (!wait_for(p, 5000, &status)) {
        TEST_FAIL("--pid %s: ppv did not exit", pid_text);
        text[0] = '\0';
        close(errp[0]);
        return -1;
    }
    read_all(errp[0], text, cap);
    close(errp[0]);
    return exit_code(status);
}

/* The two error messages of --pid that the e2e script cannot provoke. */
static void test_pid_errors(void)
{
    static char text[4096];

    /* A process that has already exited and been reaped. */
    pid_t child = fork();
    if (child == 0)
        _exit(0);
    EXPECT_TRUE(child > 0);
    waitpid(child, NULL, 0);
    char pid_text[32];
    snprintf(pid_text, sizeof pid_text, "%ld", (long)child);
    EXPECT_EQ(run_pid(pid_text, text, sizeof text), 1);
    EXPECT_CONTAINS(text, "no such process");

    /* PID 1 belongs to root: /proc/1/io is not readable by anyone else.
     * Skipped when running as root (e.g. in a container) and on kernels
     * without I/O accounting (WSL2: no /proc/PID/io at all, ppv says so
     * instead of "permission denied"). */
    if (geteuid() != 0 && access("/proc/self/io", R_OK) == 0) {
        EXPECT_EQ(run_pid("1", text, sizeof text), 1);
        EXPECT_CONTAINS(text, "permission denied");
    }
}
#endif

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: test_posix PPV GEN CHECK WORKDIR\n");
        return 2;
    }
    g_ppv = argv[1];
    g_gen = argv[2];
    g_check = argv[3];
    g_work = argv[4];
    signal(SIGPIPE, SIG_IGN);

    test_closed_stdin();
    test_closed_stdout();
    test_closed_stderr();
    test_signal(SIGINT);
    test_signal(SIGTERM);
    test_append();
    test_read_error();
    test_nonblocking();
#if defined(__linux__)
    test_pid_errors();
#endif

    return TEST_RESULT();
}
