/*
 * ppv - rate measurement.
 *
 * Counters are sampled at a fixed period (PPV_SAMPLE_NS, 100 ms) regardless
 * of how often the live line is drawn, so that short runs still yield a
 * useful number of samples and all rates mean the same thing.
 *
 * ppv_window: sliding window over the last minute for the live line.
 * ppv_stats:  constant-memory statistics over the whole run (min, max and
 *             percentiles from a log-scale histogram, ~6% resolution).
 * ppv_meter:  both, fed with (time, total) samples.
 */
#ifndef PPV_METER_H
#define PPV_METER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPV_SAMPLE_NS     UINT64_C(100000000) /* 100 ms */
#define PPV_WINDOW_NS     (UINT64_C(60) * UINT64_C(1000000000))
#define PPV_WINDOW_CAP    640 /* > 60 s / 100 ms */
#define PPV_STATS_BUCKETS 496

/* Percentiles are only reported from this many samples on (one second). */
#define PPV_PERCENTILE_MIN_SAMPLES 10

/* Rates over a window for the live line: avg = bytes / time over the whole
 * window, and the 10th, 50th and 90th percentile of the per-sample rates.
 * Percentiles instead of min/max: a single 100 ms stall does not pin the
 * line to 0 B/s for a minute, but a consumer that stalls a tenth of the
 * time shows up in p10. */
typedef struct ppv_rates {
    double avg;
    double p10;
    double p50;
    double p90;
} ppv_rates;

typedef struct ppv_window {
    uint64_t interval_ns;
    size_t first;
    size_t count;
    uint64_t t[PPV_WINDOW_CAP];
    uint64_t v[PPV_WINDOW_CAP];
} ppv_window;

void ppv_window_init(ppv_window *w, uint64_t interval_ns);
void ppv_window_push(ppv_window *w, uint64_t t_ns, uint64_t total);
/* False if fewer than two samples (no rate yet). */
bool ppv_window_rates(const ppv_window *w, ppv_rates *r);

typedef struct ppv_stats {
    uint64_t n;
    double min;
    double max;
    uint64_t hist[PPV_STATS_BUCKETS];
} ppv_stats;

void ppv_stats_init(ppv_stats *s);
void ppv_stats_add(ppv_stats *s, double rate);
/* p in [0, 1]; 0 if there are no samples. */
double ppv_stats_percentile(const ppv_stats *s, double p);

/* Histogram layout, exposed for the unit tests: the bucket of a value, and the
 * value the middle of a bucket stands for. */
unsigned ppv_stats_bucket_of(uint64_t v);
double ppv_stats_bucket_mid(unsigned b);

typedef struct ppv_meter {
    uint64_t interval_ns;
    uint64_t last_t;
    uint64_t last_v;
    ppv_window window;
    ppv_stats stats;
} ppv_meter;

void ppv_meter_init(ppv_meter *m, uint64_t interval_ns, uint64_t t0_ns, uint64_t v0);
void ppv_meter_sample(ppv_meter *m, uint64_t t_ns, uint64_t total);
/* Adds the last, partial interval if it is long enough to be meaningful. */
void ppv_meter_finish(ppv_meter *m, uint64_t t_ns, uint64_t total);

/* Whole-run summary for the report. While every sample of the run is still
 * in the window (runs up to a minute) the percentiles are exact, the same
 * values the live line shows; longer runs use the histogram (approximate,
 * about 6 % resolution). */
typedef struct ppv_rate_summary {
    uint64_t n;
    double p10;
    double p25;
    double p50;
    double p75;
    double p90;
    bool approximate; /* percentiles from the histogram */
} ppv_rate_summary;

void ppv_meter_summary(const ppv_meter *m, ppv_rate_summary *s);

#endif
