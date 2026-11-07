#include "report.h"

#include "compiler.h"
#include "format.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct sink {
    char *buf;
    size_t cap;
    size_t len;
} sink;

static void emit(sink *s, const char *fmt, ...) PPV_PRINTF(2, 3);

static void emit(sink *s, const char *fmt, ...)
{
    if (s->len + 1 >= s->cap)
        return;
    va_list ap;
    va_start(ap, fmt);
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) false positive: va_start is above */
    int n = vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    size_t room = s->cap - s->len - 1;
    s->len += (size_t)n < room ? (size_t)n : room;
}

static void sink_init(sink *s, char *buf, size_t cap)
{
    s->buf = buf;
    s->cap = cap;
    s->len = 0;
    if (cap > 0)
        buf[0] = '\0';
}

static double per_second(uint64_t bytes, uint64_t ns)
{
    return ns > 0 ? (double)bytes / ((double)ns / 1e9) : 0.0;
}

static double percent(uint64_t part, uint64_t whole)
{
    return whole > 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

/* "min 474M/s, p50 486M/s, p90 533M/s, max 545M/s (18 samples of 100ms)".
 * Percentiles are only quoted once there are enough samples for them to
 * mean something; a "~" marks values from the histogram (runs longer than
 * the window; about 6 % resolution). */
static void rate_summary(char *buf, size_t cap, const ppv_rate_summary *s, uint64_t sample_ns)
{
    if (s->n == 0) {
        snprintf(buf, cap, "no samples");
        return;
    }
    char mn[32], p50[32], p90[32], mx[32], iv[32];
    ppv_fmt_rate(mn, sizeof mn, s->min);
    ppv_fmt_rate(mx, sizeof mx, s->max);
    ppv_fmt_duration(iv, sizeof iv, sample_ns);
    if (s->n < PPV_PERCENTILE_MIN_SAMPLES) {
        snprintf(buf, cap, "min %s, max %s (%" PRIu64 " sample%s of %s, too few for percentiles)",
                 mn, mx, s->n, s->n == 1 ? "" : "s", iv);
        return;
    }
    ppv_fmt_rate(p50, sizeof p50, s->p50);
    ppv_fmt_rate(p90, sizeof p90, s->p90);
    const char *about = s->approximate ? "~" : "";
    snprintf(buf, cap, "min %s, p50 %s%s, p90 %s%s, max %s (%" PRIu64 " samples of %s)", mn, about,
             p50, about, p90, mx, s->n, iv);
}

const char *ppv_report_bottleneck(double in_pct, double out_pct)
{
    if (out_pct >= 50.0 && out_pct > in_pct)
        return "consumer-bound";
    if (in_pct >= 50.0 && in_pct > out_pct)
        return "producer-bound";
    return "balanced";
}

void ppv_report_ratio(char *buf, size_t cap, uint64_t a_ns, uint64_t b_ns)
{
    uint64_t hi = a_ns > b_ns ? a_ns : b_ns, lo = a_ns > b_ns ? b_ns : a_ns;
    if (lo == 0) {
        if (cap > 0)
            buf[0] = '\0';
        return;
    }
    double r = (double)hi / (double)lo;
    if (r >= 9.95)
        snprintf(buf, cap, "%.0f:1", r);
    else
        snprintf(buf, cap, "%.1f:1", r);
}

size_t ppv_report_transfer(const ppv_transfer_report *r, char *buf, size_t cap)
{
    sink s;
    sink_init(&s, buf, cap);

    const char *who = r->name != NULL && r->name[0] != '\0' ? r->name : "ppv";
    char total[32], exact[32], elapsed[32], avg[32], rates[256], tin[32], tout[32], ratio[32],
        chunk[32];

    ppv_fmt_size(total, sizeof total, (double)r->bytes);
    ppv_fmt_group(exact, sizeof exact, r->bytes);
    ppv_fmt_duration(elapsed, sizeof elapsed, r->elapsed_ns);
    ppv_fmt_rate(avg, sizeof avg, per_second(r->bytes, r->elapsed_ns));
    rate_summary(rates, sizeof rates, r->rates, r->sample_ns);

    emit(&s, "%s: %s (%s bytes) in %s, avg %s%s\n", who, total, exact, elapsed, avg,
         r->interrupted ? " (interrupted)" : "");
    emit(&s, "  rate     %s\n", rates);

    /* A filter with no work of its own is always inside read or write, so
     * the two shares add up to the run time; what matters is their ratio. */
    double in_pct = percent(r->in_read_ns, r->elapsed_ns);
    double out_pct = percent(r->in_write_ns, r->elapsed_ns);
    ppv_fmt_duration(tin, sizeof tin, r->in_read_ns);
    ppv_fmt_duration(tout, sizeof tout, r->in_write_ns);
    ppv_report_ratio(ratio, sizeof ratio, r->in_read_ns, r->in_write_ns);
    emit(&s, "  time     read %s (%.0f%%), write %s (%.0f%%): %s%s%s%s\n", tin, in_pct, tout,
         out_pct, ppv_report_bottleneck(in_pct, out_pct), ratio[0] != '\0' ? " (" : "", ratio,
         ratio[0] != '\0' ? ")" : "");

    ppv_fmt_size(chunk, sizeof chunk, r->ops > 0 ? (double)r->bytes / (double)r->ops : 0.0);
    emit(&s, "  io       %" PRIu64 " transfers, avg %s%s%s\n", r->ops, chunk,
         r->method != NULL ? ", " : "", r->method != NULL ? r->method : "");
    return s.len;
}

static void report_direction(sink *s, const char *label, uint64_t bytes, uint64_t elapsed_ns,
                             const ppv_rate_summary *st, uint64_t sample_ns)
{
    char total[32], exact[32], avg[32], rates[256];
    ppv_fmt_size(total, sizeof total, (double)bytes);
    ppv_fmt_group(exact, sizeof exact, bytes);
    ppv_fmt_rate(avg, sizeof avg, per_second(bytes, elapsed_ns));
    rate_summary(rates, sizeof rates, st, sample_ns);
    emit(s, "  %-6s %s (%s bytes), avg %s\n", label, total, exact, avg);
    emit(s, "         %s\n", rates);
}

size_t ppv_report_process(const ppv_process_report *r, char *buf, size_t cap)
{
    sink s;
    sink_init(&s, buf, cap);

    const char *who = r->name != NULL && r->name[0] != '\0' ? r->name : "ppv";
    char elapsed[32];
    ppv_fmt_duration(elapsed, sizeof elapsed, r->elapsed_ns);

    emit(&s, "%s: pid %" PRIu64 ", watched %s%s\n", who, r->pid, elapsed,
         r->interrupted ? " (interrupted)" : " (process exited)");
    report_direction(&s, "read", r->read_bytes, r->elapsed_ns, r->read_rates, r->sample_ns);
    report_direction(&s, "write", r->write_bytes, r->elapsed_ns, r->write_rates, r->sample_ns);
    return s.len;
}
