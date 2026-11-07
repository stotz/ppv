/*
 * ppv - rate measurement.
 *
 * ppv_window: sliding window over the last minute for the live line.
 * ppv_stats:  constant-memory statistics over the whole run (min, max and
 *             percentiles from a log-scale histogram, ~6% resolution).
 * ppv_meter:  both, fed with (time, total) samples once per interval.
 */
#ifndef PPV_METER_H
#define PPV_METER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPV_WINDOW_NS     (UINT64_C(60) * UINT64_C(1000000000))
#define PPV_WINDOW_CAP    640 /* > 60 s / 0.1 s minimum interval */
#define PPV_STATS_BUCKETS 496

typedef struct ppv_window {
    uint64_t interval_ns;
    size_t first;
    size_t count;
    uint64_t t[PPV_WINDOW_CAP];
    uint64_t v[PPV_WINDOW_CAP];
} ppv_window;

void ppv_window_init(ppv_window *w, uint64_t interval_ns);
void ppv_window_push(ppv_window *w, uint64_t t_ns, uint64_t total);
/* Rates between neighbouring samples; false if fewer than two samples. */
bool ppv_window_rates(const ppv_window *w, double *min, double *avg, double *max);

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

#endif
