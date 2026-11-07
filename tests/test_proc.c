/* Platform layer: I/O counters of a process, tested on ourselves. */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "platform/platform.h"
#include "test.h"

#include <stdio.h>

#if defined(_WIN32)
#  include <windows.h>
static uint64_t self_pid(void)
{
    return GetCurrentProcessId();
}
#else
#  include <unistd.h>
static uint64_t self_pid(void)
{
    return (uint64_t)getpid();
}
#endif

#define WRITE_BYTES (UINT64_C(4) * 1024 * 1024)

int main(void)
{
    char err[256];
    ppv_proc *p = ppv_proc_open(self_pid(), err, sizeof err);
    if (p == NULL) {
        fprintf(stderr, "skipped: %s\n", err);
        return 77;
    }

    uint64_t r0 = 0, w0 = 0, r1 = 0, w1 = 0;
    EXPECT_TRUE(ppv_proc_io(p, &r0, &w0));

    FILE *f = tmpfile();
    EXPECT_TRUE(f != NULL);
    if (f != NULL) {
        static char buf[64 * 1024];
        for (unsigned i = 0; i < WRITE_BYTES / sizeof buf; i++)
            EXPECT_TRUE(fwrite(buf, 1, sizeof buf, f) == sizeof buf);
        EXPECT_TRUE(fflush(f) == 0);
    }

    EXPECT_TRUE(ppv_proc_io(p, &r1, &w1));
    EXPECT_TRUE(w1 - w0 >= WRITE_BYTES);
    EXPECT_TRUE(r1 >= r0);
    EXPECT_TRUE(!ppv_proc_wait_exit(p, UINT64_C(1000000))); /* we are alive */

    ppv_proc_close(p);
    if (f != NULL)
        fclose(f);
    char bad[256];
    EXPECT_TRUE(ppv_proc_open(UINT64_C(4000000000), bad, sizeof bad) == NULL);

    return TEST_RESULT();
}
