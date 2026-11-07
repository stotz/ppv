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

    /* Transfer report, the example from the README */
    ppv_stats rates;
    ppv_stats_init(&rates);
    ppv_stats_add(&rates, 0.0);
    ppv_stats_add(&rates, 63488.0);
    ppv_stats_add(&rates, 65536.0);
    ppv_transfer_report r = {
        .name = NULL,
        .interrupted = false,
        .bytes = 20971520,
        .elapsed_ns = 3100 * S / 1000,
        .interval_ns = S,
        .blocked_in_ns = 31 * S / 1000,
        .blocked_out_ns = 3 * S,
        .ops = 389,
        .method = "splice",
        .rates = &rates,
    };
    size_t n = ppv_report_transfer(&r, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: 20.0M (20971520 bytes) in 3.1s, avg 6.45M/s\n");
    EXPECT_CONTAINS(text, "  rate     min 0B/s, p50 ");
    EXPECT_CONTAINS(text, ", max 64.0K/s (3 x 1.0s)\n");
    EXPECT_CONTAINS(text, "  waiting  input 0.0s (1%), output 3.0s (97%): the consumer");
    EXPECT_CONTAINS(text, "  io       389 transfers, avg 52.6K, splice\n");
    EXPECT_TRUE(strstr(text, "interrupted") == NULL);
    EXPECT_EQ(text[n - 1], '\n');

    /* Name, interrupted, no method, no samples */
    ppv_stats none;
    ppv_stats_init(&none);
    r.name = "job";
    r.interrupted = true;
    r.method = NULL;
    r.rates = &none;
    r.ops = 0;
    (void)ppv_report_transfer(&r, text, sizeof text);
    EXPECT_CONTAINS(text, "job: 20.0M (20971520 bytes) in 3.1s, avg 6.45M/s (interrupted)\n");
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
    EXPECT_CONTAINS(text, "ppv: 0B (0 bytes) in 0.0s, avg 0B/s");
    EXPECT_CONTAINS(text, "input 0.0s (0%), output 3.0s (0%): no single bottleneck");

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
    ppv_stats_add(&rd, 1e6);
    ppv_stats_add(&wr, 2e6);
    ppv_process_report p = {
        .name = NULL,
        .interrupted = false,
        .pid = 4242,
        .elapsed_ns = 12 * S,
        .interval_ns = S,
        .read_bytes = 12000000,
        .write_bytes = 24000000,
        .read_rates = &rd,
        .write_rates = &wr,
    };
    n = ppv_report_process(&p, text, sizeof text);
    EXPECT_EQ(n, strlen(text));
    EXPECT_CONTAINS(text, "ppv: pid 4242, watched 12.0s (process exited)\n");
    EXPECT_CONTAINS(text, "  read   11.4M (12000000 bytes), avg 977K/s\n");
    EXPECT_CONTAINS(text, "         min 977K/s, p50 ");
    EXPECT_CONTAINS(text, "  write  22.9M (24000000 bytes), avg 1.91M/s\n");
    p.interrupted = true;
    p.name = "w";
    (void)ppv_report_process(&p, text, sizeof text);
    EXPECT_CONTAINS(text, "w: pid 4242, watched 12.0s (interrupted)\n");

    return TEST_RESULT();
}
