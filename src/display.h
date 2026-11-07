/*
 * ppv - the live status line on stderr.
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

#include <stdbool.h>
#include <stddef.h>

#define PPV_LINE_DEFAULT 79  /* visible characters if the terminal width is unknown */
#define PPV_LINE_MAX     255 /* hard cap, whatever the terminal says */
#define PPV_LINE_BUF     (PPV_LINE_MAX + 2) /* + "\n" or "\r", + NUL */

typedef struct ppv_display {
    bool enabled;
    size_t last_len;
    const char *name; /* optional prefix, may be NULL */
} ppv_display;

void ppv_display_init(ppv_display *d, bool enabled, const char *name);

/* Builds the next line into out (capacity >= PPV_LINE_BUF) and returns its
 * length, 0 if nothing is to be written. columns is the terminal width or 0
 * if unknown. final = true ends the line with "\n" so it stays on screen. */
size_t ppv_display_render(ppv_display *d, const char *body, bool final, unsigned columns,
                          char *out);

#endif
