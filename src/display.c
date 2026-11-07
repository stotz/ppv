#include "display.h"

#include "format.h"

#include <stdio.h>
#include <string.h>

#define FIELD_BUF 40 /* a formatted value is < 16 characters; padding stays bounded */

void ppv_display_init(ppv_display *d, bool enabled, const char *name)
{
    memset(d, 0, sizeof *d);
    d->enabled = enabled;
    d->name = name != NULL && name[0] != '\0' ? name : NULL;
}

/* Right-aligns value to the widest value field idx has shown so far. */
static void field(ppv_display *d, size_t idx, const char *value, char *out)
{
    size_t len = strlen(value);
    if (len > d->width[idx])
        d->width[idx] = len;
    size_t pad = d->width[idx] - len;
    if (pad > FIELD_BUF - 1 - len)
        pad = FIELD_BUF - 1 - len;
    memset(out, ' ', pad);
    memcpy(out + pad, value, len + 1);
}

static void size_field(ppv_display *d, size_t idx, uint64_t bytes, char *out)
{
    char v[32];
    ppv_fmt_size(v, sizeof v, (double)bytes);
    field(d, idx, v, out);
}

static void rate_field(ppv_display *d, size_t idx, const ppv_rates *r, double value, char *out)
{
    char v[32];
    if (r == NULL)
        snprintf(v, sizeof v, "-");
    else
        ppv_fmt_rate(v, sizeof v, value);
    field(d, idx, v, out);
}

size_t ppv_display_transfer(ppv_display *d, uint64_t bytes, const ppv_rates *rates, bool final,
                            unsigned columns, char *out)
{
    if (!d->enabled)
        return 0;
    char total[FIELD_BUF], mn[FIELD_BUF], av[FIELD_BUF], p90[FIELD_BUF], mx[FIELD_BUF];
    size_field(d, 0, bytes, total);
    rate_field(d, 1, rates, rates != NULL ? rates->min : 0.0, mn);
    rate_field(d, 2, rates, rates != NULL ? rates->avg : 0.0, av);
    rate_field(d, 3, rates, rates != NULL ? rates->p90 : 0.0, p90);
    rate_field(d, 4, rates, rates != NULL ? rates->max : 0.0, mx);

    char body[PPV_LINE_BUF + 5 * FIELD_BUF];
    snprintf(body, sizeof body, "%s[%s, min %s, avg %s, p90 %s, max %s]",
             d->name != NULL ? d->name : "ppv", total, mn, av, p90, mx);
    return ppv_display_render(d, body, final, columns, out);
}

size_t ppv_display_process(ppv_display *d, uint64_t read_bytes, const ppv_rates *read_rates,
                           uint64_t write_bytes, const ppv_rates *write_rates, bool final,
                           unsigned columns, char *out)
{
    if (!d->enabled)
        return 0;
    char rt[FIELD_BUF], ra[FIELD_BUF], wt[FIELD_BUF], wa[FIELD_BUF];
    size_field(d, 0, read_bytes, rt);
    rate_field(d, 1, read_rates, read_rates != NULL ? read_rates->avg : 0.0, ra);
    size_field(d, 2, write_bytes, wt);
    rate_field(d, 3, write_rates, write_rates != NULL ? write_rates->avg : 0.0, wa);

    char body[PPV_LINE_BUF + 4 * FIELD_BUF];
    snprintf(body, sizeof body, "%s[R %s avg %s | W %s avg %s]", d->name != NULL ? d->name : "ppv",
             rt, ra, wt, wa);
    return ppv_display_render(d, body, final, columns, out);
}

size_t ppv_display_render(ppv_display *d, const char *body, bool final, unsigned columns, char *out)
{
    if (!d->enabled)
        return 0;

    /* Stay one short of the width so the cursor never reaches the last column. */
    size_t width = PPV_LINE_DEFAULT;
    if (columns > 0)
        width = columns - 1 < PPV_LINE_MAX ? columns - 1 : PPV_LINE_MAX;

    int n = snprintf(out, PPV_LINE_BUF, "%s", body);
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
