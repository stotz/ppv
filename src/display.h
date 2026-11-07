/*
 * ppv - the live status line on stderr.
 *
 * Only "\r" and spaces are used: the line is written, padded to cover the
 * previous one, and followed by "\r". It never reaches the last column, so
 * it cannot wrap on any terminal (conhost, Windows Terminal, mintty, xterm).
 */
#ifndef PPV_DISPLAY_H
#define PPV_DISPLAY_H

#include <stdbool.h>
#include <stddef.h>

#define PPV_LINE_MAX 79

typedef struct ppv_display {
    bool enabled;
    size_t last_len;
    const char *name;
} ppv_display;

void ppv_display_init(ppv_display *d, bool enabled, const char *name);
/* final = true ends the line with "\n" so it stays on screen. */
void ppv_display_draw(ppv_display *d, const char *body, bool final);

#endif
