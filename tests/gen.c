/*
 * ppv_gen SIZE [DELAY_MS] - write SIZE bytes of the test pattern to stdout,
 * optionally pausing DELAY_MS milliseconds after every 64 KiB chunk (a slow,
 * long-lived producer for the --pid test).
 */
#if !defined(_WIN32)
#  define _POSIX_C_SOURCE 200809L /* nanosleep */
#endif

#include "pattern.h"

#include <stdlib.h>

#if defined(_WIN32)
#  include <windows.h>
static void sleep_ms(unsigned ms)
{
    Sleep(ms);
}
#else
#  include <time.h>
static void sleep_ms(unsigned ms)
{
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    (void)nanosleep(&ts, NULL);
}
#endif

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
        return 2;
    uint64_t size = strtoull(argv[1], NULL, 10);
    unsigned delay_ms = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 0;
    set_binary(stdout);

    static unsigned char buf[65536];
    uint64_t pos = 0;
    while (pos < size) {
        size_t n = size - pos < sizeof buf ? (size_t)(size - pos) : sizeof buf;
        for (size_t k = 0; k < n; k++)
            buf[k] = pattern_at(pos + k);
        if (fwrite(buf, 1, n, stdout) != n)
            return 0; /* reader went away - fine for the "closed" test */
        pos += n;
        if (delay_ms > 0) {
            if (fflush(stdout) != 0)
                return 0;
            sleep_ms(delay_ms);
        }
    }
    return fflush(stdout) == 0 ? 0 : 1;
}
