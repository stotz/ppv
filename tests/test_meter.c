#include "meter.h"
#include "test.h"

#define S UINT64_C(1000000000)

static ppv_meter m; /* large; keep it off the stack */

int main(void)
{
    /* Window: rates 100, 200 -> p10 100, p90 200, avg 150 (bytes / time) */
    ppv_meter_init(&m, S, 0, 0);
    ppv_rates r;
    uint64_t total;
    EXPECT_TRUE(!ppv_window_rates(&m.window, &r));
    ppv_meter_sample(&m, 1 * S, 100);
    EXPECT_TRUE(ppv_window_rates(&m.window, &r)); /* one rate: all equal */
    EXPECT_NEAR(r.p10, 100, 1e-9);
    EXPECT_NEAR(r.p50, 100, 1e-9);
    EXPECT_NEAR(r.p90, 100, 1e-9);
    EXPECT_NEAR(r.avg, 100, 1e-9);
    ppv_meter_sample(&m, 2 * S, 300);
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, 100, 1e-9); /* ceil(0.1 * 2) = 1st of 2 */
    EXPECT_NEAR(r.p50, 100, 1e-9); /* ceil(0.5 * 2) = 1st of 2 */
    EXPECT_NEAR(r.avg, 150, 1e-9);
    EXPECT_NEAR(r.p90, 200, 1e-9); /* ceil(0.9 * 2) = 2nd of 2 */

    /* avg is bytes / time, not the mean of the rates: an uneven sample
     * spacing must not change it. */
    ppv_meter_init(&m, S, 0, 0);
    ppv_meter_sample(&m, 1 * S, 1000);       /* 1000 B/s for 1 s */
    ppv_meter_sample(&m, 4 * S, 1000 + 300); /* 100 B/s for 3 s */
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.avg, 1300.0 / 4.0, 1e-9);
    EXPECT_NEAR(r.p10, 100, 1e-9);
    EXPECT_NEAR(r.p90, 1000, 1e-9);

    /* Percentiles of the window: nearest rank over the per-sample rates. */
    ppv_meter_init(&m, S, 0, 0);
    total = 0;
    for (uint64_t k = 1; k <= 20; k++) {
        total += k * 10; /* rates 10, 20, ..., 200 */
        ppv_meter_sample(&m, k * S, total);
    }
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p90, 180, 1e-9); /* ceil(0.9 * 20) = 18th of 20 */
    EXPECT_NEAR(r.p50, 100, 1e-9); /* 10th of 20 */
    EXPECT_NEAR(r.p10, 20, 1e-9);  /* 2nd of 20 */
    EXPECT_NEAR(r.avg, (double)total / 20.0, 1e-9);

    /* One 100 ms stall in a minute: p10 unmoved, where min would be 0. */
    ppv_meter_init(&m, PPV_SAMPLE_NS, 0, 0);
    total = 0;
    for (uint64_t k = 1; k <= 600; k++) {
        if (k != 300)
            total += 1000;
        ppv_meter_sample(&m, k * PPV_SAMPLE_NS, total);
    }
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, 10000, 1e-6);
    EXPECT_NEAR(r.p50, 10000, 1e-6);
    /* ... but a consumer stalled 20 % of the time shows in p10 */
    ppv_meter_init(&m, PPV_SAMPLE_NS, 0, 0);
    total = 0;
    for (uint64_t k = 1; k <= 600; k++) {
        if (k % 5 != 0)
            total += 1000;
        ppv_meter_sample(&m, k * PPV_SAMPLE_NS, total);
    }
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, 0, 1e-9);
    EXPECT_NEAR(r.p50, 10000, 1e-6);
    EXPECT_NEAR(r.avg, 8000, 1e-6);
    ppv_meter_init(&m, S, 0, 0);

    /* After 2 minutes at 1000 B/s the early samples are out of the window. */
    ppv_meter_sample(&m, 1 * S, 100);
    ppv_meter_sample(&m, 2 * S, 300);
    total = 300;
    for (uint64_t t = 3; t <= 122; t++) {
        total += 1000;
        ppv_meter_sample(&m, t * S, total);
    }
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, 1000, 1e-9);
    EXPECT_NEAR(r.avg, 1000, 1e-9);
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

    /* Ring buffer wrap-around: the real 100 ms period, far more than PPV_WINDOW_CAP samples. */
    ppv_meter_init(&m, PPV_SAMPLE_NS, 0, 0);
    total = 0;
    for (uint64_t k = 1; k <= UINT64_C(3) * PPV_WINDOW_CAP; k++) {
        total += 100;
        ppv_meter_sample(&m, k * S / 10, total);
    }
    EXPECT_TRUE(m.window.count <= 602 && m.window.count >= 600);
    EXPECT_TRUE(m.window.first < PPV_WINDOW_CAP);
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, 1000, 1e-6);
    EXPECT_NEAR(r.avg, 1000, 1e-6);
    EXPECT_NEAR(r.p50, 1000, 1e-6);
    EXPECT_NEAR(r.p90, 1000, 1e-6);
    EXPECT_TRUE(m.stats.n == UINT64_C(3) * PPV_WINDOW_CAP);
    EXPECT_NEAR(ppv_stats_percentile(&m.stats, 0.5), 1000, 1000 * 0.07);

    /* Whole-run summary: exact (and equal to the live p90) while every
     * sample is still in the window, from the histogram once the window
     * has evicted something. */
    ppv_rate_summary sum;
    ppv_meter_init(&m, PPV_SAMPLE_NS, 0, 0);
    ppv_meter_summary(&m, &sum);
    EXPECT_TRUE(sum.n == 0 && !sum.approximate);
    total = 0;
    for (uint64_t k = 1; k <= 20; k++) {
        total += k * 1000; /* rates 10K, 20K, ..., 200K B/s */
        ppv_meter_sample(&m, k * PPV_SAMPLE_NS, total);
    }
    ppv_meter_summary(&m, &sum);
    EXPECT_TRUE(sum.n == 20 && !sum.approximate);
    EXPECT_NEAR(sum.p10, 20000, 1e-6);  /* exact: 2nd of 20 */
    EXPECT_NEAR(sum.p25, 50000, 1e-6);  /* 5th */
    EXPECT_NEAR(sum.p50, 100000, 1e-6); /* 10th */
    EXPECT_NEAR(sum.p75, 150000, 1e-6); /* 15th */
    EXPECT_NEAR(sum.p90, 180000, 1e-6); /* 18th */
    EXPECT_TRUE(ppv_window_rates(&m.window, &r));
    EXPECT_NEAR(r.p10, sum.p10, 1e-9); /* live line and report agree */
    EXPECT_NEAR(r.p50, sum.p50, 1e-9);
    EXPECT_NEAR(r.p90, sum.p90, 1e-9);
    /* keep sampling past the window: histogram, marked approximate */
    for (uint64_t k = 21; k <= 700; k++) {
        total += 100000;
        ppv_meter_sample(&m, k * PPV_SAMPLE_NS, total);
    }
    ppv_meter_summary(&m, &sum);
    EXPECT_TRUE(sum.n == 700 && sum.approximate);
    EXPECT_NEAR(sum.p10, 1000000, 1000000 * 0.07);
    EXPECT_NEAR(sum.p50, 1000000, 1000000 * 0.07);
    EXPECT_NEAR(sum.p90, 1000000, 1000000 * 0.07);

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
    volatile double zero = 0.0; /* runtime NaN: MSVC rejects 0.0 / 0.0 (C2124) */
    ppv_stats_add(&s, zero / zero);
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
