#include "display.h"

#include <stdio.h>
#include <string.h>

void ppv_display_init(ppv_display *d, bool enabled, const char *name)
{
    d->enabled = enabled;
    d->last_len = 0;
    d->name = name != NULL && name[0] != '\0' ? name : NULL;
}

size_t ppv_display_render(ppv_display *d, const char *body, bool final, unsigned columns, char *out)
{
    if (!d->enabled)
        return 0;

    /* Stay one short of the width so the cursor never reaches the last column. */
    size_t width = PPV_LINE_DEFAULT;
    if (columns > 0)
        width = columns - 1 < PPV_LINE_MAX ? columns - 1 : PPV_LINE_MAX;

    int n = d->name != NULL ? snprintf(out, PPV_LINE_BUF, "%s: %s", d->name, body)
                            : snprintf(out, PPV_LINE_BUF, "%s", body);
    size_t len = n < 0 ? 0 : (size_t)n;
    if (len > width)
        len = width;

    /* Pad over whatever the previous (longer) line left behind. */
    size_t visible = len;
    while (len < d->last_len && len < width)
        out[len++] = ' ';
    out[len++] = final ? '\n' : '\r';
    out[len] = '\0';

    d->last_len = final ? 0 : visible;
    return len;
}
