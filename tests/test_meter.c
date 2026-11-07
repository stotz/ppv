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
    ppv_meter_init(&m, S, 0, 0);
    ppv_meter_sample(&m, S, 1000);
    ppv_meter_finish(&m, S + S / 10, 2000); /* 0.1 s tail after a full interval: dropped */
    EXPECT_TRUE(m.stats.n == 1);
    ppv_meter_finish(&m, S + S / 2, 2000); /* half an interval: taken */
    EXPECT_TRUE(m.stats.n == 2);

    /* Samples that do not advance in time or go backwards are ignored. */
    ppv_meter_init(&m, S, 10 * S, 500);
    ppv_meter_sample(&m, 10 * S, 600);
    ppv_meter_sample(&m, 9 * S, 700);
    EXPECT_TRUE(m.stats.n == 0);
    ppv_meter_sample(&m, 11 * S, 100); /* counter went backwards: clamped, rate 0 */
    EXPECT_TRUE(m.stats.n == 1);
    EXPECT_NEAR(m.stats.max, 0, 1e-9);

    /* Ring buffer wrap-around: 0.1 s interval, far more than PPV_WINDOW_CAP samples. */
    ppv_meter_init(&m, S / 10, 0, 0);
    total = 0;
    for (uint64_t k = 1; k <= UINT64_C(3) * PPV_WINDOW_CAP; k++) {
        total += 100;
        ppv_meter_sample(&m, k * S / 10, total);
    }
    EXPECT_TRUE(m.window.count <= 602 && m.window.count >= 600);
    EXPECT_TRUE(m.window.first < PPV_WINDOW_CAP);
    EXPECT_TRUE(ppv_window_rates(&m.window, &mn, &av, &mx));
    EXPECT_NEAR(mn, 1000, 1e-6);
    EXPECT_NEAR(av, 1000, 1e-6);
    EXPECT_NEAR(mx, 1000, 1e-6);
    EXPECT_TRUE(m.stats.n == UINT64_C(3) * PPV_WINDOW_CAP);
    EXPECT_NEAR(ppv_stats_percentile(&m.stats, 0.5), 1000, 1000 * 0.07);

    /* Histogram buckets: monotonic, in range, and the middle is within one
     * sub-bucket (1/8 of the power of two) of the value. */
    static const uint64_t probes[] = {0,
                                      1,
                                      7,
                                      8,
                                      9,
                                      15,
                                      16,
                                      17,
                                      100,
                                      1023,
                                      1024,
                                      1025,
                                      65535,
                                      65536,
                                      1000000,
                                      UINT64_C(1) << 40,
                                      (UINT64_C(1) << 63) - 1,
                                      UINT64_C(1) << 63,
                                      UINT64_MAX};
    unsigned prev = 0;
    for (size_t i = 0; i < sizeof probes / sizeof probes[0]; i++) {
        uint64_t v = probes[i];
        unsigned b = ppv_stats_bucket_of(v);
        EXPECT_TRUE(b < PPV_STATS_BUCKETS);
        EXPECT_TRUE(b >= prev);
        prev = b;
        double mid = ppv_stats_bucket_mid(b);
        if (v < 8) {
            EXPECT_NEAR(mid, (double)v, 1e-9);
        } else {
            double tol = (double)v / 8.0 + 1.0;
            EXPECT_NEAR(mid, (double)v, tol);
        }
    }
    EXPECT_EQ(ppv_stats_bucket_of(UINT64_MAX), PPV_STATS_BUCKETS - 1);
    for (uint64_t v = 0; v < 4096; v++)
        EXPECT_TRUE(ppv_stats_bucket_of(v) <= ppv_stats_bucket_of(v + 1));

    /* Very large and invalid rates do not break the histogram. */
    ppv_stats_init(&s);
    ppv_stats_add(&s, 1e30);
    ppv_stats_add(&s, -5.0);
    ppv_stats_add(&s, 0.0 / 0.0);
    EXPECT_TRUE(s.n == 3);
    EXPECT_NEAR(s.min, 0, 1e-9);
    EXPECT_NEAR(s.max, 1e30, 1e20);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.5), 0, 1e-9);
    EXPECT_NEAR(ppv_stats_percentile(&s, 1.0), 1e30, 1e20);

    /* Percentile edge cases */
    ppv_stats_init(&s);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.5), 0, 1e-9);
    ppv_stats_add(&s, 5000.0);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.5), 5000, 1e-9);
    EXPECT_NEAR(ppv_stats_percentile(&s, 0.0), 5000, 1e-9);
    EXPECT_NEAR(ppv_stats_percentile(&s, 1.0), 5000, 1e-9);

    return TEST_RESULT();
}
