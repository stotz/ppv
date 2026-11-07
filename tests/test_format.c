#include "format.h"
#include "test.h"

static const char *size(double v)
{
    static char buf[32];
    ppv_fmt_size(buf, sizeof buf, v);
    return buf;
}

static const char *dur(double seconds)
{
    static char buf[32];
    ppv_fmt_duration(buf, sizeof buf, (uint64_t)(seconds * 1e9));
    return buf;
}

int main(void)
{
    EXPECT_STR(size(0), "0B");
    EXPECT_STR(size(-5), "0B");
    EXPECT_STR(size(999), "999B");
    EXPECT_STR(size(1000), "0.98K");
    EXPECT_STR(size(1024), "1.00K");
    EXPECT_STR(size(1536), "1.50K");
    EXPECT_STR(size(10 * 1024), "10.0K");
    EXPECT_STR(size(105.0 * 1024 * 1024), "105M");
    EXPECT_STR(size(6.335 * 1024 * 1024 * 1024), "6.33G");
    EXPECT_STR(size(1023.0 * 1024 * 1024 * 1024), "1.00T"); /* 0.999T rounds up */
    EXPECT_STR(size(1000.0 * 1024 * 1024 * 1024), "0.98T");

    char rate[32];
    ppv_fmt_rate(rate, sizeof rate, 17163834.0);
    EXPECT_STR(rate, "16.4M/s");

    /* Below a second in milliseconds, so that 18 ms does not read as "0.0s". */
    EXPECT_STR(dur(0), "0ms");
    EXPECT_STR(dur(0.0004), "0ms");
    EXPECT_STR(dur(0.0006), "1ms");
    EXPECT_STR(dur(0.018), "18ms");
    EXPECT_STR(dur(0.1), "100ms");
    EXPECT_STR(dur(0.9994), "999ms");
    EXPECT_STR(dur(0.9996), "1.0s");
    EXPECT_STR(dur(1.8), "1.8s");
    EXPECT_STR(dur(12.34), "12.3s");
    EXPECT_STR(dur(59.94), "59.9s");
    EXPECT_STR(dur(59.96), "1m00s");
    EXPECT_STR(dur(383), "6m23s");
    EXPECT_STR(dur(3723), "1h02m03s");
    EXPECT_STR(dur(2 * 86400 + 3 * 3600 + 4 * 60), "2d03h04m");

    char g[32];
    ppv_fmt_group(g, sizeof g, 0);
    EXPECT_STR(g, "0");
    ppv_fmt_group(g, sizeof g, 999);
    EXPECT_STR(g, "999");
    ppv_fmt_group(g, sizeof g, 1000);
    EXPECT_STR(g, "1_000");
    ppv_fmt_group(g, sizeof g, 930086912);
    EXPECT_STR(g, "930_086_912");
    ppv_fmt_group(g, sizeof g, UINT64_MAX);
    EXPECT_STR(g, "18_446_744_073_709_551_615");
    ppv_fmt_group(g, 5, 1234567); /* truncated, still terminated */
    EXPECT_STR(g, "1_23");
    ppv_fmt_group(g, 1, 7);
    EXPECT_STR(g, "");

    return TEST_RESULT();
}
