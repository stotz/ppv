#include "report.h"

#include "format.h"
#include "platform/platform.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>

static void emit(const char *fmt, ...) PPV_PRINTF(1, 2);

static void emit(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    ppv_stderr_write(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

static double per_second(uint64_t bytes, uint64_t ns)
{
    return ns > 0 ? (double)bytes / ((double)ns / 1e9) : 0.0;
}

static double percent(uint64_t part, uint64_t whole)
{
    return whole > 0 ? 100.0 * (double)part / (double)whole : 0.0;
}

static void rate_summary(char *buf, size_t cap, const ppv_stats *s, uint64_t interval_ns)
{
    if (s->n == 0) {
        snprintf(buf, cap, "no samples");
        return;
    }
    char mn[32], p50[32], p90[32], mx[32], iv[32];
    ppv_fmt_rate(mn, sizeof mn, s->min);
    ppv_fmt_rate(p50, sizeof p50, ppv_stats_percentile(s, 0.50));
    ppv_fmt_rate(p90, sizeof p90, ppv_stats_percentile(s, 0.90));
    ppv_fmt_rate(mx, sizeof mx, s->max);
    ppv_fmt_duration(iv, sizeof iv, interval_ns);
    snprintf(buf, cap, "min %s, p50 %s, p90 %s, max %s (%" PRIu64 " x %s)", mn, p50, p90, mx,
             s->n, iv);
}

static const char *bottleneck(double in_pct, double out_pct)
{
    if (out_pct >= 50.0 && out_pct > in_pct)
        return "the consumer (output side) is the bottleneck";
    if (in_pct >= 50.0 && in_pct > out_pct)
        return "the producer (input side) is the bottleneck";
    return "no single bottleneck";
}

void ppv_report_transfer(const ppv_transfer_report *r)
{
    const char *who = r->name != NULL ? r->name : "ppv";
    char total[32], elapsed[32], avg[32], rates[256], bin[32], bout[32], chunk[32];

    ppv_fmt_size(total, sizeof total, (double)r->bytes);
    ppv_fmt_duration(elapsed, sizeof elapsed, r->elapsed_ns);
    ppv_fmt_rate(avg, sizeof avg, per_second(r->bytes, r->elapsed_ns));
    rate_summary(rates, sizeof rates, r->rates, r->interval_ns);

    emit("%s: %s (%" PRIu64 " bytes) in %s, avg %s%s\n", who, total, r->bytes, elapsed, avg,
         r->interrupted ? " (interrupted)" : "");
    emit("  rate     %s\n", rates);

    double in_pct = percent(r->blocked_in_ns, r->elapsed_ns);
    double out_pct = percent(r->blocked_out_ns, r->elapsed_ns);
    ppv_fmt_duration(bin, sizeof bin, r->blocked_in_ns);
    ppv_fmt_duration(bout, sizeof bout, r->blocked_out_ns);
    emit("  waiting  input %s (%.0f%%), output %s (%.0f%%): %s\n", bin, in_pct, bout, out_pct,
         bottleneck(in_pct, out_pct));

    ppv_fmt_size(chunk, sizeof chunk, r->ops > 0 ? (double)r->bytes / (double)r->ops : 0.0);
    emit("  io       %" PRIu64 " transfers, avg %s%s%s\n", r->ops, chunk,
         r->method != NULL ? ", " : "", r->method != NULL ? r->method : "");
}

static void report_direction(const char *label, uint64_t bytes, uint64_t elapsed_ns,
                             const ppv_stats *s, uint64_t interval_ns)
{
    char total[32], avg[32], rates[256];
    ppv_fmt_size(total, sizeof total, (double)bytes);
    ppv_fmt_rate(avg, sizeof avg, per_second(bytes, elapsed_ns));
    rate_summary(rates, sizeof rates, s, interval_ns);
    emit("  %-6s %s (%" PRIu64 " bytes), avg %s\n", label, total, bytes, avg);
    emit("         %s\n", rates);
}

void ppv_report_process(const ppv_process_report *r)
{
    const char *who = r->name != NULL ? r->name : "ppv";
    char elapsed[32];
    ppv_fmt_duration(elapsed, sizeof elapsed, r->elapsed_ns);

    emit("%s: pid %" PRIu64 ", watched %s%s\n", who, r->pid, elapsed,
         r->interrupted ? " (interrupted)" : " (process exited)");
    report_direction("read", r->read_bytes, r->elapsed_ns, r->read_rates, r->interval_ns);
    report_direction("write", r->write_bytes, r->elapsed_ns, r->write_rates, r->interval_ns);
}
