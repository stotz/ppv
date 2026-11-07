/* The -s statistics: wording, numbers and the bottleneck heuristic. */
#include "report.h"
#include "test.h"

#include <stdio.h>

#define S UINT64_C(1000000000)

static char text[PPV_REPORT_BUF];

int main(void)
{
    /* Bottleneck: needs >= 50 % on one side and more than the other */
    EXPECT_STR(ppv_report_bottleneck(1.0, 99.0), "the consumer (output side) is the bottleneck");
    EXPECT_STR(ppv_report_bottleneck(80.0, 10.0), "the producer (input side) is the bottleneck");
    EXPECT_STR(ppv_report_bottleneck(50.0, 10.0), "the producer (input side) is the bottleneck");
    EXPECT_STR(ppv_report_bottleneck(49.9, 10.0), "no single bottleneck");
    EXPECT_STR(ppv_report_bottleneck(50.0, 50.0), "no single bottleneck");
    EXPECT_STR(ppv_report_bottleneck(0.0, 0.0), "no single bottleneck");

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
    ppv_stats rates;
    ppv_stats_init(&rates);
    ppv_stats_add(&rates, 0.0);
    for (int i = 0; i < 30; i++)
        ppv_stats_add(&rates, i < 15 ? 63488.0 : 65536.0);
    ppv_transfer_report r = {
        .name = NULL,
        .interrupted = false,
        .bytes = 20971520,
        .elapsed_ns = 3100 * S / 1000,
        .sample_ns = S / 10,
        .in_read_ns = 31 * S / 1000,
        .in_write_ns = 3 * S,
        .ops = 389,
        .method = "splice",
        .rates = &rates,
    };
    size_t n = ppv_report_transfer(&r, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: 20.0M (20_971_520 bytes) in 3.1s, avg 6.45M/s\n");
    EXPECT_CONTAINS(text, "  rate     min 0B/s, p50 ");
    EXPECT_CONTAINS(text, ", max 64.0K/s (31 samples of 100ms)\n");
    EXPECT_CONTAINS(text, "  time     in read 31ms (1%), in write 3.0s (97%): "
                          "the consumer (output side) is the bottleneck (97:1)\n");
    EXPECT_CONTAINS(text, "  io       389 transfers, avg 52.6K, splice\n");
    EXPECT_TRUE(strstr(text, "interrupted") == NULL);
    EXPECT_EQ(text[n - 1], '\n');

    /* Too few samples: min and max only, and it says so */
    ppv_stats few;
    ppv_stats_init(&few);
    few.n = 0;
    r.rates = &few;
    ppv_stats_add(&few, 474e6);
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(
        text, "  rate     min 452M/s, max 452M/s (1 sample of 100ms, too few for percentiles)\n");
    for (int i = 0; i < 8; i++)
        ppv_stats_add(&few, 500e6);
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "(9 samples of 100ms, too few for percentiles)\n");
    EXPECT_TRUE(strstr(text, "p50") == NULL);
    ppv_stats_add(&few, 500e6); /* the tenth: percentiles appear */
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, ", p50 ");
    EXPECT_CONTAINS(text, "(10 samples of 100ms)\n");
    r.rates = &rates;

    /* Name, interrupted, no method, no samples */
    ppv_stats none;
    ppv_stats_init(&none);
    r.name = "job";
    r.interrupted = true;
    r.method = NULL;
    r.rates = &none;
    r.ops = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "job: 20.0M (20_971_520 bytes) in 3.1s, avg 6.45M/s (interrupted)\n");
    EXPECT_CONTAINS(text, "  rate     no samples\n");
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
    EXPECT_CONTAINS(text, "in read 31ms (0%), in write 3.0s (0%): no single bottleneck (97:1)");
    r.in_read_ns = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "in read 0ms (0%), in write 3.0s (0%): no single bottleneck\n");

    /* Truncation: always NUL terminated, length reported honestly */
    char small[40];
    n = ppv_report_transfer(&r, small, sizeof small);
    EXPECT_EQ(n, sizeof small - 1);
    EXPECT_EQ(strlen(small), sizeof small - 1);
    n = ppv_report_transfer(&r, small, 1);
    EXPECT_EQ(n, 0);
    EXPECT_STR(small, "");

    /* Process report */
    ppv_stats rd, wr;
    ppv_stats_init(&rd);
    ppv_stats_init(&wr);
    for (int i = 0; i < 120; i++) {
        ppv_stats_add(&rd, 1e6);
        ppv_stats_add(&wr, 2e6);
    }
    ppv_process_report p = {
        .name = NULL,
        .interrupted = false,
        .pid = 4242,
        .elapsed_ns = 12 * S,
        .sample_ns = S / 10,
        .read_bytes = 12000000,
        .write_bytes = 24000000,
        .read_rates = &rd,
        .write_rates = &wr,
    };
    n = ppv_report_process(&p, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: pid 4242, watched 12.0s (process exited)\n");
    EXPECT_CONTAINS(text, "  read   11.4M (12_000_000 bytes), avg 977K/s\n");
    EXPECT_CONTAINS(text, "         min 977K/s, p50 ");
    EXPECT_CONTAINS(text, "(120 samples of 100ms)\n");
    EXPECT_CONTAINS(text, "  write  22.9M (24_000_000 bytes), avg 1.91M/s\n");
    p.interrupted = true;
    p.name = "w";
    (void)ppv_report_process(&p, text, sizeof text);
    EXPECT_CONTAINS(text, "w: pid 4242, watched 12.0s (interrupted)\n");

    return TEST_RESULT();
}
