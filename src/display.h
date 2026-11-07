/*
 * ppv - the live status line on stderr.
 *
 *   ppv[887M, min  98M/s, avg  99M/s, p90 100M/s, max 102M/s]
 *   ppv[R 0B avg 0B/s | W 3.84G avg 1.11G/s]                     (--pid)
 *
 * Every numeric field is right-aligned to the widest value it has shown so
 * far, so the labels do not jump when a value crosses a digit or unit
 * boundary; columns only ever grow.
 *
 * Only "\r" and spaces are used: the line is written, padded to cover the
 * previous one, and followed by "\r". It never reaches the last column, so
 * it cannot wrap on any terminal (conhost, Windows Terminal, mintty, xterm).
 *
 * Rendering is pure (no I/O) so that it can be unit tested; main.c writes the
 * result to stderr.
 */
#ifndef PPV_DISPLAY_H
#define PPV_DISPLAY_H

#include "meter.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPV_LINE_DEFAULT 79  /* visible characters if the terminal width is unknown */
#define PPV_LINE_MAX     255 /* hard cap, whatever the terminal says */
#define PPV_LINE_BUF     (PPV_LINE_MAX + 2) /* + "\n" or "\r", + NUL */
#define PPV_FIELDS       5 /* numeric fields per line (total, min, avg, p90, max) */

typedef struct ppv_display {
    bool enabled;
    size_t last_len;
    const char *name;         /* label before the bracket; "ppv" if NULL */
    size_t width[PPV_FIELDS]; /* widest value shown per field so far */
} ppv_display;

void ppv_display_init(ppv_display *d, bool enabled, const char *name);

/* The transfer line. rates may be NULL (no rate yet: "-"). Returns the
 * length of the line written to out (capacity >= PPV_LINE_BUF), 0 if the
 * display is disabled. columns is the terminal width or 0 if unknown;
 * final = true ends the line with "\n" so it stays on screen. */
size_t ppv_display_transfer(ppv_display *d, uint64_t bytes, const ppv_rates *rates, bool final,
                            unsigned columns, char *out);

/* The --pid line: total and average per direction. */
size_t ppv_display_process(ppv_display *d, uint64_t read_bytes, const ppv_rates *read_rates,
                           uint64_t write_bytes, const ppv_rates *write_rates, bool final,
                           unsigned columns, char *out);

/* Low level: body as given, cut to the width, padded over the previous line. */
size_t ppv_display_render(ppv_display *d, const char *body, bool final, unsigned columns,
                          char *out);

#endif
