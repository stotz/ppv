#include "display.h"

#include "platform/platform.h"

#include <stdio.h>
#include <string.h>

void ppv_display_init(ppv_display *d, bool enabled, const char *name)
{
    d->enabled = enabled;
    d->last_len = 0;
    d->name = name;
}

void ppv_display_draw(ppv_display *d, const char *body, bool final)
{
    if (!d->enabled || !ppv_stderr_in_foreground())
        return;

    char line[PPV_LINE_MAX + 2];
    int n = d->name != NULL ? snprintf(line, sizeof line, "%s: %s", d->name, body)
                            : snprintf(line, sizeof line, "%s", body);
    size_t len = n < 0 ? 0 : (size_t)n;
    if (len > PPV_LINE_MAX)
        len = PPV_LINE_MAX;

    size_t visible = len;
    while (len < d->last_len)
        line[len++] = ' ';
    line[len++] = final ? '\n' : '\r';

    ppv_stderr_write(line, len);
    d->last_len = final ? 0 : visible;
}
