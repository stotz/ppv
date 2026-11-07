#include "format.h"

#include <inttypes.h>
#include <stdio.h>

static const char units[] = "BKMGTPE";

void ppv_fmt_size(char *buf, size_t cap, double v)
{
    size_t u = 0;

    if (!(v > 0.0)) /* also catches NaN */
        v = 0.0;
    while (v >= 999.5 && u + 2 < sizeof units) {
        v /= 1024.0;
        u++;
    }

    if (u == 0)
        snprintf(buf, cap, "%.0fB", v);
    else if (v < 9.995)
        snprintf(buf, cap, "%.2f%c", v, units[u]);
    else if (v < 99.95)
        snprintf(buf, cap, "%.1f%c", v, units[u]);
    else
        snprintf(buf, cap, "%.0f%c", v, units[u]);
}

void ppv_fmt_rate(char *buf, size_t cap, double bytes_per_sec)
{
    char size[32];
    ppv_fmt_size(size, sizeof size, bytes_per_sec);
    snprintf(buf, cap, "%s/s", size);
}

void ppv_fmt_duration(char *buf, size_t cap, uint64_t ns)
{
    if (ns < UINT64_C(999500000)) {
        snprintf(buf, cap, "%" PRIu64 "ms", (ns + UINT64_C(500000)) / UINT64_C(1000000));
        return;
    }
    if (ns < UINT64_C(59950000000)) {
        snprintf(buf, cap, "%.1fs", (double)ns / 1e9);
        return;
    }

    uint64_t t = (ns + UINT64_C(500000000)) / UINT64_C(1000000000);
    uint64_t d = t / 86400, h = t / 3600 % 24, m = t / 60 % 60, s = t % 60;

    if (d > 0)
        snprintf(buf, cap, "%" PRIu64 "d%02" PRIu64 "h%02" PRIu64 "m", d, h, m);
    else if (h > 0)
        snprintf(buf, cap, "%" PRIu64 "h%02" PRIu64 "m%02" PRIu64 "s", h, m, s);
    else
        snprintf(buf, cap, "%" PRIu64 "m%02" PRIu64 "s", m, s);
}

void ppv_fmt_group(char *buf, size_t cap, uint64_t v)
{
    char digits[21];
    int n = snprintf(digits, sizeof digits, "%" PRIu64, v);
    size_t len = n < 0 ? 0 : (size_t)n;
    size_t out = 0;
    for (size_t i = 0; i < len && out + 1 < cap; i++) {
        if (i > 0 && (len - i) % 3 == 0 && out + 1 < cap)
            buf[out++] = '_';
        if (out + 1 < cap)
            buf[out++] = digits[i];
    }
    if (cap > 0)
        buf[out] = '\0';
}
