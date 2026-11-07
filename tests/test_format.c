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

    EXPECT_STR(dur(0), "0.0s");
    EXPECT_STR(dur(12.34), "12.3s");
    EXPECT_STR(dur(383), "6m23s");
    EXPECT_STR(dur(3723), "1h02m03s");
    EXPECT_STR(dur(2 * 86400 + 3 * 3600 + 4 * 60), "2d03h04m");

    return TEST_RESULT();
}
