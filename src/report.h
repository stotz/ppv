/* ppv - final statistics (-s). */
#ifndef PPV_REPORT_H
#define PPV_REPORT_H

#include "meter.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct ppv_transfer_report {
    const char *name;
    bool interrupted;
    uint64_t bytes;
    uint64_t elapsed_ns;
    uint64_t interval_ns;
    uint64_t blocked_in_ns;
    uint64_t blocked_out_ns;
    uint64_t ops;
    const char *method; /* may be NULL */
    const ppv_stats *rates;
} ppv_transfer_report;

typedef struct ppv_process_report {
    const char *name;
    bool interrupted;
    uint64_t pid;
    uint64_t elapsed_ns;
    uint64_t interval_ns;
    uint64_t read_bytes;
    uint64_t write_bytes;
    const ppv_stats *read_rates;
    const ppv_stats *write_rates;
} ppv_process_report;

void ppv_report_transfer(const ppv_transfer_report *r);
void ppv_report_process(const ppv_process_report *r);

#endif
