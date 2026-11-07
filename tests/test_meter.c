#include "meter.h"
#include "test.h"

#define S UINT64_C(1000000000)

static ppv_meter m; /* large; keep it off the stack */

int main(void)
{
    /* Window: rates 100, 200 -> min 100, max 200, avg 150 */
    ppv_meter_init(&m, S, 0, 0);
    double mn, av, mx;
    EXPECT_TRUE(!ppv_window_rates(&m.window, &mn, &av, &mx));
    ppv_meter_sample(&m, 1 * S, 100);
    ppv_meter_sample(&m, 2 * S, 300);
    EXPECT_TRUE(ppv_window_rates(&m.window, &mn, &av, &mx));
    EXPECT_NEAR(mn, 100, 1e-9);
    EXPECT_NEAR(mx, 200, 1e-9);
    EXPECT_NEAR(av, 150, 1e-9);

    /* After 2 minutes at 1000 B/s the early samples are out of the window. */
    uint64_t total = 300;
    for (uint64_t t = 3; t <= 122; t++) {
        total += 1000;
        ppv_meter_sample(&m, t * S, total);
    }
    EXPECT_TRUE(ppv_window_rates(&m.window, &mn, &av, &mx));
    EXPECT_NEAR(mn, 1000, 1e-9);
    EXPECT_NEAR(av, 1000, 1e-9);
    EXPECT_TRUE(m.window.count <= 62);

    /* Whole-run stats still remember the slow start. */
    EXPECT_NEAR(m.stats.min, 100, 1e-9);
    EXPECT_NEAR(m.stats.max, 1000, 1e-9);
    EXPECT_TRUE(m.stats.n == 122);
    EXPECT_NEAR(ppv_stats_percentile(&m.stats, 0.5), 1000, 1000 * 0.07);

    /* Percentiles of 1..100 MB/s: within histogram resolution (~6%). */
    ppv_stats s;
    ppv_stats_init(&s);
    for (int i = 1; i <= 100; i++)
        ppv_stats_add(&s, i * 1e6);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.5), 50e6, 50e6 * 0.07);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.9), 90e6, 90e6 * 0.07);
    EXPECT_NEAR(ppv_stats_percentile(&s, 1.0), 100e6, 1e-3);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.0), 1e6, 1e-3);

    /* A short tail is ignored, unless it is the only sample. */
    ppv_meter_init(&m, S, 0, 0);
    ppv_meter_finish(&m, S / 10, 1000);
    EXPECT_TRUE(m.stats.n == 1);
    EXPECT_NEAR(m.stats.max, 10000, 1e-6);

    return TEST_RESULT();
}
