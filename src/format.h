/* ppv - human readable numbers (binary units: K = 1024). */
#ifndef PPV_FORMAT_H
#define PPV_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* "0B", "999B", "0.98K", "1.50M", "105M" - at most 3 significant digits. */
void ppv_fmt_size(char *buf, size_t cap, double v);

/* ppv_fmt_size() + "/s" */
void ppv_fmt_rate(char *buf, size_t cap, double bytes_per_sec);

/* "12.3s", "6m23s", "1h02m03s", "2d03h04m" */
void ppv_fmt_duration(char *buf, size_t cap, uint64_t ns);

#endif
