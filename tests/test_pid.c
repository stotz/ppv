/*
 * End-to-end test of --pid: a slow producer (ppv_gen writing to a file) is
 * watched by ppv; the final statistics must report the bytes it wrote.
 *
 *   test_pid PPV GEN WORKDIR
 *
 * Exit 77 = skipped (platform without process I/O counters, e.g. macOS or a
 * Linux kernel without CONFIG_TASK_IO_ACCOUNTING).
 */
#include "test.h"

#include <stdbool.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Not SIZE: windows.h defines a type of that name. */
#define GEN_BYTES UINT64_C(12800000) /* about 200 chunks of 64 KiB ... */
#define DELAY_MS  5                  /* ... about one second in total */

#if defined(_WIN32)
#  include <windows.h>

typedef HANDLE proc_t;

static HANDLE open_out(const char *path)
{
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    return CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
}

/* Starts `cmdline` with stdout/stderr redirected to the given handles. */
static proc_t spawn(char *cmdline, HANDLE out, HANDLE err, uint64_t *pid)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = out;
    si.hStdError = err;
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", (unsigned long)GetLastError());
        exit(1);
    }
    CloseHandle(pi.hThread);
    *pid = pi.dwProcessId;
    return pi.hProcess;
}

static int wait_exit(proc_t p)
{
    DWORD code = 1;
    WaitForSingleObject(p, INFINITE);
    GetExitCodeProcess(p, &code);
    CloseHandle(p);
    return (int)code;
}

#else
#  include <fcntl.h>
#  include <sys/wait.h>
#  include <unistd.h>

typedef pid_t proc_t;

static int open_out(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(path);
        exit(1);
    }
    return fd;
}

/* Starts argv with stdout/stderr redirected to the given descriptors. */
static proc_t spawn(char **argv, int out, int err, uint64_t *pid)
{
    pid_t p = fork();
    if (p < 0) {
        perror("fork");
        exit(1);
    }
    if (p == 0) {
        if (dup2(out, STDOUT_FILENO) < 0 || dup2(err, STDERR_FILENO) < 0)
            _exit(127);
        execv(argv[0], argv);
        _exit(127);
    }
    *pid = (uint64_t)p;
    return p;
}

static int wait_exit(proc_t p)
{
    int status = 0;
    if (waitpid(p, &status, 0) < 0)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}
#endif

/* "  write  11.4M (12000000 bytes), avg ..." -> 12000000 */
static bool parse_bytes(const char *text, const char *label, uint64_t *out)
{
    const char *p = strstr(text, label);
    if (p == NULL)
        return false;
    p = strchr(p, '(');
    if (p == NULL)
        return false;
    char *end = NULL;
    unsigned long long v = strtoull(p + 1, &end, 10);
    if (end == p + 1 || strncmp(end, " bytes", 6) != 0)
        return false;
    *out = v;
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: test_pid PPV GEN WORKDIR\n");
        return 2;
    }
    char *ppv = argv[1], *gen = argv[2];
    const char *work = argv[3];

    char data_path[1024], err_path[1024];
    snprintf(data_path, sizeof data_path, "%s/test_pid_data.bin", work);
    snprintf(err_path, sizeof err_path, "%s/test_pid_stderr.txt", work);

    /* Start the producer, then ppv watching it. */
    uint64_t gen_pid = 0, ppv_pid = 0;
    char size_arg[32], delay_arg[32], pid_arg[32];
    snprintf(size_arg, sizeof size_arg, "%" PRIu64, GEN_BYTES);
    snprintf(delay_arg, sizeof delay_arg, "%u", (unsigned)DELAY_MS);

#if defined(_WIN32)
    HANDLE data = open_out(data_path);
    HANDLE errh = open_out(err_path);
    char gen_cmd[2048], ppv_cmd[2048];
    snprintf(gen_cmd, sizeof gen_cmd, "\"%s\" %s %s", gen, size_arg, delay_arg);
    proc_t gen_proc = spawn(gen_cmd, data, GetStdHandle(STD_ERROR_HANDLE), &gen_pid);
    snprintf(pid_arg, sizeof pid_arg, "%" PRIu64, gen_pid);
    snprintf(ppv_cmd, sizeof ppv_cmd, "\"%s\" --pid %s -q -s -i 0.1", ppv, pid_arg);
    proc_t ppv_proc = spawn(ppv_cmd, GetStdHandle(STD_OUTPUT_HANDLE), errh, &ppv_pid);
    CloseHandle(data);
    CloseHandle(errh);
#else
    int data = open_out(data_path);
    int errfd = open_out(err_path);
    char *gen_argv[] = {gen, size_arg, delay_arg, NULL};
    proc_t gen_proc = spawn(gen_argv, data, STDERR_FILENO, &gen_pid);
    snprintf(pid_arg, sizeof pid_arg, "%" PRIu64, gen_pid);
    /* argv must be writable char pointers: no string literals */
    static char opt_pid[] = "--pid", opt_q[] = "-q", opt_s[] = "-s", opt_i[] = "-i",
                opt_iv[] = "0.1";
    char *ppv_argv[] = {ppv, opt_pid, pid_arg, opt_q, opt_s, opt_i, opt_iv, NULL};
    proc_t ppv_proc = spawn(ppv_argv, STDOUT_FILENO, errfd, &ppv_pid);
    close(data);
    close(errfd);
#endif

    int ppv_rc = wait_exit(ppv_proc);
    int gen_rc = wait_exit(gen_proc);
    EXPECT_EQ(gen_rc, 0);

    /* Read what ppv reported. */
    static char text[8192];
    FILE *f = fopen(err_path, "rb");
    size_t n = f != NULL ? fread(text, 1, sizeof text - 1, f) : 0;
    text[n] = '\0';
    if (f != NULL)
        fclose(f);
    remove(data_path);
    remove(err_path);

    if (ppv_rc == 1 &&
        (strstr(text, "not supported") != NULL || strstr(text, "without I/O accounting") != NULL)) {
        fprintf(stderr, "skipped: %s", text);
        return 77;
    }
    if (ppv_rc != 0)
        TEST_FAIL("ppv exit code %d:\n%s", ppv_rc, text);

    EXPECT_CONTAINS(text, "(process exited)");
    EXPECT_CONTAINS(text, "  read   ");
    EXPECT_CONTAINS(text, "  write  ");
    EXPECT_TRUE(strstr(text, "[") == NULL); /* -q: no live line */

    /* The producer ran for about a second; even if the last interval is lost
     * (Linux) most of its output must have been seen. */
    uint64_t written = 0;
    EXPECT_TRUE(parse_bytes(text, "  write  ", &written));
    if (written < GEN_BYTES / 4 || written > GEN_BYTES + UINT64_C(1024) * 1024)
        TEST_FAIL("write counter %" PRIu64 " not plausible for %" PRIu64 " bytes:\n%s", written,
                  GEN_BYTES, text);
    EXPECT_TRUE(strstr(text, "no samples") == NULL);

    return TEST_RESULT();
}
