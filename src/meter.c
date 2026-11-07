#include "meter.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Sliding window                                                           */

static size_t at(const ppv_window *w, size_t i)
{
    return (w->first + i) % PPV_WINDOW_CAP;
}

void ppv_window_init(ppv_window *w, uint64_t interval_ns)
{
    w->interval_ns = interval_ns;
    w->first = 0;
    w->count = 0;
}

void ppv_window_push(ppv_window *w, uint64_t t_ns, uint64_t total)
{
    if (w->count == PPV_WINDOW_CAP) {
        w->first = at(w, 1);
        w->count--;
    }
    size_t i = at(w, w->count);
    w->t[i] = t_ns;
    w->v[i] = total;
    w->count++;

    /* Keep about one minute; half an interval of slack absorbs timer jitter. */
    uint64_t keep = PPV_WINDOW_NS + w->interval_ns / 2;
    while (w->count > 2 && t_ns - w->t[w->first] > keep) {
        w->first = at(w, 1);
        w->count--;
    }
}

static int compare_double(const void *x, const void *y)
{
    double a = *(const double *)x, b = *(const double *)y;
    return a < b ? -1 : a > b ? 1 : 0;
}

/* The per-sample rates of the window, sorted ascending. At most 640 doubles
 * once per drawn line: negligible. Returns their number (count - 1). */
static size_t sorted_rates(const ppv_window *w, double *rates)
{
    size_t n = 0;
    for (size_t k = 1; k < w->count; k++) {
        size_t a = at(w, k - 1), b = at(w, k);
        double dt = (double)(w->t[b] - w->t[a]) / 1e9;
        rates[n++] = dt > 0.0 ? (double)(w->v[b] - w->v[a]) / dt : 0.0;
    }
    qsort(rates, n, sizeof rates[0], compare_double);
    return n;
}

/* Nearest-rank percentile of sorted values: the smallest value with at least
 * the share p of the samples at or below it. tenths = 5 for p50, 9 for p90. */
static double nearest_rank(const double *sorted, size_t n, unsigned tenths)
{
    size_t rank = (tenths * n + 9) / 10; /* ceil(p n), >= 1 for n >= 1 */
    if (rank == 0)
        rank = 1;
    return sorted[rank - 1];
}

bool ppv_window_rates(const ppv_window *w, ppv_rates *r)
{
    if (w->count < 2)
        return false;

    double rates[PPV_WINDOW_CAP];
    size_t n = sorted_rates(w, rates);

    size_t a = w->first, b = at(w, w->count - 1);
    double span = (double)(w->t[b] - w->t[a]) / 1e9;
    r->min = rates[0];
    r->max = rates[n - 1];
    r->p90 = nearest_rank(rates, n, 9);
    r->avg = span > 0.0 ? (double)(w->v[b] - w->v[a]) / span : 0.0;
    return true;
}

/* ------------------------------------------------------------------------ */
/* Whole-run statistics                                                     */
/*                                                                          */
/* Bucket layout: values 0..7 map to themselves; above that every power of */
/* two is split into 8 sub-buckets (3 mantissa bits), integer math only.    */

unsigned ppv_stats_bucket_of(uint64_t v)
{
    if (v < 8)
        return (unsigned)v;
    unsigned msb = 63;
    while (((v >> msb) & 1u) == 0)
        msb--;
    return 8u + (msb - 3u) * 8u + (unsigned)((v >> (msb - 3u)) & 7u);
}

double ppv_stats_bucket_mid(unsigned b)
{
    if (b < 8)
        return (double)b;
    unsigned msb = (b - 8u) / 8u + 3u;
    unsigned sub = (b - 8u) % 8u;
    double width = (double)(UINT64_C(1) << (msb - 3u));
    return (double)(8u + sub) * width + width / 2.0;
}

void ppv_stats_init(ppv_stats *s)
{
    memset(s, 0, sizeof *s);
}

void ppv_stats_add(ppv_stats *s, double rate)
{
    if (!(rate > 0.0)) /* also catches NaN */
        rate = 0.0;
    if (s->n == 0 || rate < s->min)
        s->min = rate;
    if (s->n == 0 || rate > s->max)
        s->max = rate;
    uint64_t v = rate >= 1.8e19 ? UINT64_MAX : (uint64_t)rate;
    s->hist[ppv_stats_bucket_of(v)]++;
    s->n++;
}

double ppv_stats_percentile(const ppv_stats *s, double p)
{
    if (s->n == 0)
        return 0.0;
    if (p <= 0.0)
        return s->min;
    if (p >= 1.0)
        return s->max;

    double exact = p * (double)s->n;
    uint64_t rank = (uint64_t)exact;
    if ((double)rank < exact)
        rank++;
    if (rank == 0)
        rank = 1;

    uint64_t seen = 0;
    for (unsigned b = 0; b < PPV_STATS_BUCKETS; b++) {
        seen += s->hist[b];
        if (seen >= rank) {
            double v = ppv_stats_bucket_mid(b);
            return v < s->min ? s->min : v > s->max ? s->max : v;
        }
    }
    return s->max;
}

/* ------------------------------------------------------------------------ */
/* Meter                                                                    */

void ppv_meter_init(ppv_meter *m, uint64_t interval_ns, uint64_t t0_ns, uint64_t v0)
{
    m->interval_ns = interval_ns;
    m->last_t = t0_ns;
    m->last_v = v0;
    ppv_window_init(&m->window, interval_ns);
    ppv_window_push(&m->window, t0_ns, v0);
    ppv_stats_init(&m->stats);
}

void ppv_meter_sample(ppv_meter *m, uint64_t t_ns, uint64_t total)
{
    if (t_ns <= m->last_t)
        return;
    if (total < m->last_v) /* counters never go backwards; be defensive */
        total = m->last_v;
    double dt = (double)(t_ns - m->last_t) / 1e9;
    ppv_stats_add(&m->stats, (double)(total - m->last_v) / dt);
    ppv_window_push(&m->window, t_ns, total);
    m->last_t = t_ns;
    m->last_v = total;
}

void ppv_meter_finish(ppv_meter *m, uint64_t t_ns, uint64_t total)
{
    if (t_ns <= m->last_t)
        return;
    /* A very short tail would distort min/max; skip it unless it is all we have. */
    if (t_ns - m->last_t >= m->interval_ns / 2 || m->stats.n == 0)
        ppv_meter_sample(m, t_ns, total);
}

void ppv_meter_summary(const ppv_meter *m, ppv_rate_summary *s)
{
    s->n = m->stats.n;
    s->min = m->stats.min;
    s->max = m->stats.max;
    s->p50 = 0.0;
    s->p90 = 0.0;
    s->approximate = false;
    if (s->n == 0)
        return;

    /* Nothing evicted yet: the window holds every sample of the run. */
    if (m->window.count >= 2 && m->window.count - 1 == s->n) {
        double rates[PPV_WINDOW_CAP];
        size_t n = sorted_rates(&m->window, rates);
        s->p50 = nearest_rank(rates, n, 5);
        s->p90 = nearest_rank(rates, n, 9);
        return;
    }
    s->approximate = true;
    s->p50 = ppv_stats_percentile(&m->stats, 0.50);
    s->p90 = ppv_stats_percentile(&m->stats, 0.90);
}
