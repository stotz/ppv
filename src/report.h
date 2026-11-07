/*
 * ppv - final statistics (-s).
 *
 * The report is rendered into a caller supplied buffer (no I/O) so that it can
 * be unit tested; main.c writes it to stderr.
 */
#ifndef PPV_REPORT_H
#define PPV_REPORT_H

#include "meter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPV_REPORT_BUF 1024 /* enough for either report, incl. a long --name */

typedef struct ppv_transfer_report {
    const char *name; /* may be NULL */
    bool interrupted;
    uint64_t bytes;
    uint64_t elapsed_ns;
    uint64_t sample_ns;  /* sampling period of `rates` */
    uint64_t in_read_ns; /* time spent inside read calls (waiting + copying) */
    uint64_t in_write_ns;
    uint64_t ops;
    const char *method; /* may be NULL */
    const ppv_stats *rates;
} ppv_transfer_report;

typedef struct ppv_process_report {
    const char *name; /* may be NULL */
    bool interrupted;
    uint64_t pid;
    uint64_t elapsed_ns;
    uint64_t sample_ns;
    uint64_t read_bytes;
    uint64_t write_bytes;
    const ppv_stats *read_rates;
    const ppv_stats *write_rates;
} ppv_process_report;

/* Both return the length of the text written to buf (always NUL terminated,
 * truncated if cap is too small). */
size_t ppv_report_transfer(const ppv_transfer_report *r, char *buf, size_t cap);
size_t ppv_report_process(const ppv_process_report *r, char *buf, size_t cap);

/* Which side of the pipe ppv waited for, from the shares of the run time
 * spent in read and in write calls. Exposed for the unit tests. */
const char *ppv_report_bottleneck(double in_pct, double out_pct);

/* "60:1", "1.3:1"; "" if one side is zero. Larger side first. */
void ppv_report_ratio(char *buf, size_t cap, uint64_t a_ns, uint64_t b_ns);

#endif
