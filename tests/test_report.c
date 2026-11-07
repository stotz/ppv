/* The -s statistics: wording, numbers and the bottleneck heuristic. */
#include "report.h"
#include "test.h"

#include <stdio.h>

#define S UINT64_C(1000000000)

static char text[PPV_REPORT_BUF];

int main(void)
{
    /* Bottleneck: needs >= 50 % on one side and more than the other */
    EXPECT_STR(ppv_report_bottleneck(1.0, 99.0), "consumer-bound");
    EXPECT_STR(ppv_report_bottleneck(80.0, 10.0), "producer-bound");
    EXPECT_STR(ppv_report_bottleneck(50.0, 10.0), "producer-bound");
    EXPECT_STR(ppv_report_bottleneck(49.9, 10.0), "balanced");
    EXPECT_STR(ppv_report_bottleneck(50.0, 50.0), "balanced");
    EXPECT_STR(ppv_report_bottleneck(0.0, 0.0), "balanced");

    /* Ratio of the two sides, larger first; empty if one side is zero */
    char ratio[32];
    ppv_report_ratio(ratio, sizeof ratio, 1700 * S / 1000, 18 * S / 1000);
    EXPECT_STR(ratio, "94:1");
    ppv_report_ratio(ratio, sizeof ratio, 18 * S / 1000, 1700 * S / 1000);
    EXPECT_STR(ratio, "94:1");
    ppv_report_ratio(ratio, sizeof ratio, 13 * S, 10 * S);
    EXPECT_STR(ratio, "1.3:1");
    ppv_report_ratio(ratio, sizeof ratio, 995 * S, 100 * S);
    EXPECT_STR(ratio, "10:1");
    ppv_report_ratio(ratio, sizeof ratio, 994 * S, 100 * S);
    EXPECT_STR(ratio, "9.9:1");
    ppv_report_ratio(ratio, sizeof ratio, S, S);
    EXPECT_STR(ratio, "1.0:1");
    ppv_report_ratio(ratio, sizeof ratio, S, 0);
    EXPECT_STR(ratio, "");
    ppv_report_ratio(ratio, sizeof ratio, 0, 0);
    EXPECT_STR(ratio, "");

    /* Transfer report, the example from the README (31 samples of 100 ms) */
    ppv_rate_summary rates = {.n = 31,
                              .p10 = 0.0,
                              .p25 = 61440.0,
                              .p50 = 63488.0,
                              .p75 = 64512.0,
                              .p90 = 65536.0,
                              .approximate = false};
    ppv_transfer_report r = {
        .name = NULL,
        .interrupted = false,
        .bytes = 20971520,
        .elapsed_ns = 3100 * S / 1000,
        .in_read_ns = 31 * S / 1000,
        .in_write_ns = 3 * S,
        .ops = 389,
        .method = "splice",
        .rates = &rates,
    };
    size_t n = ppv_report_transfer(&r, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: 20.0M (20_971_520 bytes) in 3.1s, avg 6.45M/s, 31 samples\n");
    EXPECT_CONTAINS(text,
                    "  rate     p10 0B/s, p25 60.0K/s, p50 62.0K/s, p75 63.0K/s, p90 64.0K/s\n");
    EXPECT_CONTAINS(text, "  time     read 31ms (1%), write 3.0s (97%): consumer-bound (97:1)\n");
    EXPECT_CONTAINS(text, "  io       389 transfers, avg 52.6K, splice\n");
    EXPECT_TRUE(strstr(text, "interrupted") == NULL);
    EXPECT_EQ(text[n - 1], '\n');

    /* Too few samples: no rate line; the headline says how many there were */
    ppv_rate_summary few = {
        .n = 1, .p10 = 474e6, .p25 = 474e6, .p50 = 474e6, .p75 = 474e6, .p90 = 474e6};
    r.rates = &few;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "avg 6.45M/s, 1 sample\n  time     ");
    EXPECT_TRUE(strstr(text, "rate") == NULL);
    few.n = 9;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "avg 6.45M/s, 9 samples\n  time     ");
    EXPECT_TRUE(strstr(text, "p50") == NULL);
    few.n = 10; /* the tenth: percentiles appear */
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "avg 6.45M/s, 10 samples\n");
    EXPECT_CONTAINS(text,
                    "  rate     p10 452M/s, p25 452M/s, p50 452M/s, p75 452M/s, p90 452M/s\n");

    /* Histogram-based percentiles (runs longer than the window) are marked "~" */
    few.n = 800;
    few.approximate = true;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "avg 6.45M/s, 800 samples\n");
    EXPECT_CONTAINS(text,
                    "  rate     p10 ~452M/s, p25 ~452M/s, p50 ~452M/s, p75 ~452M/s, p90 ~452M/s\n");
    r.rates = &rates;

    /* Name, interrupted, no method, no samples */
    ppv_rate_summary none = {0};
    r.name = "job";
    r.interrupted = true;
    r.method = NULL;
    r.rates = &none;
    r.ops = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(
        text, "job: 20.0M (20_971_520 bytes) in 3.1s, avg 6.45M/s, no samples (interrupted)\n");
    EXPECT_TRUE(strstr(text, "rate") == NULL);
    EXPECT_CONTAINS(text, "  io       0 transfers, avg 0B\n");

    /* An empty name falls back to "ppv" */
    r.name = "";
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_TRUE(strncmp(text, "ppv: ", 5) == 0);

    /* Zero elapsed time: no division by zero, 0 % */
    r.elapsed_ns = 0;
    r.bytes = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "ppv: 0B (0 bytes) in 0ms, avg 0B/s");
    EXPECT_CONTAINS(text, "read 31ms (0%), write 3.0s (0%): balanced (97:1)");
    r.in_read_ns = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "read 0ms (0%), write 3.0s (0%): balanced\n");

    /* Truncation: always NUL terminated, length reported honestly */
    char small[40];
    n = ppv_report_transfer(&r, small, sizeof small);
    EXPECT_EQ(n, sizeof small - 1);
    EXPECT_EQ(strlen(small), sizeof small - 1);
    n = ppv_report_transfer(&r, small, 1);
    EXPECT_EQ(n, 0);
    EXPECT_STR(small, "");

    /* Process report */
    ppv_rate_summary rd = {.n = 120, .p10 = 1e6, .p25 = 1e6, .p50 = 1e6, .p75 = 1e6, .p90 = 1e6};
    ppv_rate_summary wr = {.n = 120, .p10 = 2e6, .p25 = 2e6, .p50 = 2e6, .p75 = 2e6, .p90 = 2e6};
    ppv_process_report p = {
        .name = NULL,
        .interrupted = false,
        .pid = 4242,
        .elapsed_ns = 12 * S,
        .read_bytes = 12000000,
        .write_bytes = 24000000,
        .read_rates = &rd,
        .write_rates = &wr,
    };
    n = ppv_report_process(&p, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: pid 4242, watched 12.0s (process exited)\n");
    EXPECT_CONTAINS(text, "  read   11.4M (12_000_000 bytes), avg 977K/s, 120 samples\n");
    EXPECT_CONTAINS(text, "         p10 977K/s, p25 977K/s, p50 977K/s, p75 977K/s, p90 977K/s\n");
    EXPECT_CONTAINS(text, "  write  22.9M (24_000_000 bytes), avg 1.91M/s, 120 samples\n");
    EXPECT_CONTAINS(text, "         p10 1.91M/s, ");
    /* a direction without samples has no percentile line either */
    ppv_rate_summary zero = {0};
    p.read_rates = &zero;
    (void)ppv_report_process(&p, text, sizeof text);
    EXPECT_CONTAINS(text, "  read   11.4M (12_000_000 bytes), avg 977K/s, no samples\n  write  ");
    p.read_rates = &rd;
    p.interrupted = true;
    p.name = "w";
    (void)ppv_report_process(&p, text, sizeof text);
    EXPECT_CONTAINS(text, "w: pid 4242, watched 12.0s (interrupted)\n");

    return TEST_RESULT();
}
