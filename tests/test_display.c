/* The live line: labels, stable column widths, truncation, padding, line
 * endings, terminal width. */
#include "display.h"
#include "test.h"

#include <stdio.h>

static ppv_display d;
static char line[PPV_LINE_BUF];

/* Renders a raw body and checks the whole output against expected. */
static void expect_line(const char *body, bool final, unsigned cols, const char *expected)
{
    size_t n = ppv_display_render(&d, body, final, cols, line);
    EXPECT_EQ(n, strlen(expected));
    EXPECT_EQ(strlen(line), n);
    EXPECT_STR(line, expected);
}

static ppv_rates rates(double min, double avg, double p90, double max)
{
    ppv_rates r = {min, avg, p90, max};
    return r;
}

#define KIB(n) (UINT64_C(n) * 1024)
#define MIB(n) (UINT64_C(n) * 1024 * 1024)

int main(void)
{
    /* Disabled: nothing at all */
    ppv_display_init(&d, false, "x");
    EXPECT_EQ(ppv_display_render(&d, "[1]", false, 0, line), 0);
    EXPECT_EQ(ppv_display_transfer(&d, 1, NULL, false, 0, line), 0);
    EXPECT_EQ(ppv_display_process(&d, 1, NULL, 2, NULL, false, 0, line), 0);

    /* ---- raw rendering ---- */

    /* Plain lines end with \r, the final one with \n */
    ppv_display_init(&d, true, NULL);
    expect_line("[105M, 12.3M/s]", false, 0, "[105M, 12.3M/s]\r");
    expect_line("[106M, 12.4M/s]", true, 0, "[106M, 12.4M/s]\n");

    /* A shorter line is padded to cover the previous one, then not any more */
    ppv_display_init(&d, true, NULL);
    expect_line("[12345]", false, 0, "[12345]\r");
    expect_line("[1]", false, 0, "[1]    \r");
    expect_line("[2]", false, 0, "[2]\r");
    expect_line("[12345]", true, 0, "[12345]\n");
    expect_line("[1]", false, 0, "[1]\r"); /* nothing to cover after a final line */

    /* Unknown width: 79 visible characters at most */
    char body[400];
    memset(body, 'x', sizeof body - 1);
    body[sizeof body - 1] = '\0';
    ppv_display_init(&d, true, NULL);
    size_t n = ppv_display_render(&d, body, false, 0, line);
    EXPECT_EQ(n, PPV_LINE_DEFAULT + 1);
    EXPECT_TRUE(line[PPV_LINE_DEFAULT] == '\r');

    /* Known width: one column less than the terminal, capped at PPV_LINE_MAX */
    n = ppv_display_render(&d, body, false, 30, line);
    EXPECT_EQ(n, 30);
    EXPECT_TRUE(line[29] == '\r');
    n = ppv_display_render(&d, body, false, 200, line);
    EXPECT_EQ(n, 200);
    n = ppv_display_render(&d, body, false, 10000, line);
    EXPECT_EQ(n, PPV_LINE_MAX + 1);
    EXPECT_TRUE(line[PPV_LINE_MAX] == '\r');

    /* A terminal that shrank: padding never exceeds the current width */
    ppv_display_init(&d, true, NULL);
    (void)ppv_display_render(&d, body, false, 100, line); /* 99 visible */
    n = ppv_display_render(&d, "[1]", false, 20, line);
    EXPECT_EQ(n, 20);
    EXPECT_STR(line, "[1]                \r");

    /* Degenerate width */
    ppv_display_init(&d, true, NULL);
    expect_line("[1]", false, 1, "\r");
    expect_line("[1]", true, 2, "[\n");

    /* ---- transfer line ---- */

    /* No rate yet: dashes; the label is "ppv" or the name */
    ppv_display_init(&d, true, NULL);
    (void)ppv_display_transfer(&d, 0, NULL, false, 0, line);
    EXPECT_STR(line, "ppv[0B, min -, avg -, p90 -, max -]\r");
    ppv_display_init(&d, true, "job");
    (void)ppv_display_transfer(&d, 0, NULL, false, 0, line);
    EXPECT_STR(line, "job[0B, min -, avg -, p90 -, max -]\r");
    ppv_display_init(&d, true, ""); /* empty name = no name */
    (void)ppv_display_transfer(&d, 0, NULL, false, 0, line);
    EXPECT_STR(line, "ppv[0B, min -, avg -, p90 -, max -]\r");

    /* Values in their labelled places */
    ppv_display_init(&d, true, NULL);
    ppv_rates r = rates(98e6, 99e6, 100e6, 102e6);
    (void)ppv_display_transfer(&d, MIB(887), &r, false, 0, line);
    EXPECT_STR(line, "ppv[887M, min 93.5M/s, avg 94.4M/s, p90 95.4M/s, max 97.3M/s]\r");

    /* Columns only grow: a value that got shorter is right-aligned to the
     * widest value seen in that field, so the labels do not jump. */
    ppv_display_init(&d, true, NULL);
    r = rates(98 * 1024, 99 * 1024, 100 * 1024, 102 * 1024);
    (void)ppv_display_transfer(&d, KIB(887), &r, false, 0, line);
    EXPECT_STR(line, "ppv[887K, min 98.0K/s, avg 99.0K/s, p90 100K/s, max 102K/s]\r");
    r = rates(100 * 1024, 101 * 1024, 103 * 1024, 105 * 1024);
    (void)ppv_display_transfer(&d, KIB(890), &r, false, 0, line);
    EXPECT_STR(line, "ppv[890K, min  100K/s, avg  101K/s, p90 103K/s, max 105K/s]\r");
    r = rates(98 * 1024, 99 * 1024, 100 * 1024, 102 * 1024);
    (void)ppv_display_transfer(&d, KIB(895), &r, false, 0, line);
    EXPECT_STR(line, "ppv[895K, min 98.0K/s, avg 99.0K/s, p90 100K/s, max 102K/s]\r");
    /* unit change widens the total field for good */
    (void)ppv_display_transfer(&d, MIB(1), &r, false, 0, line);
    EXPECT_STR(line, "ppv[1.00M, min 98.0K/s, avg 99.0K/s, p90 100K/s, max 102K/s]\r");
    (void)ppv_display_transfer(&d, KIB(900), &r, true, 0, line);
    EXPECT_STR(line, "ppv[ 900K, min 98.0K/s, avg 99.0K/s, p90 100K/s, max 102K/s]\n");
    /* a dash after real values is padded too */
    (void)ppv_display_transfer(&d, KIB(900), NULL, false, 0, line);
    EXPECT_STR(line, "ppv[ 900K, min       -, avg       -, p90      -, max      -]\r");

    /* The line is cut like any other if the terminal is narrow */
    ppv_display_init(&d, true, "abcdefghij");
    n = ppv_display_transfer(&d, 1, NULL, false, 8, line);
    EXPECT_EQ(n, 8);
    EXPECT_STR(line, "abcdefg\r");

    /* ---- --pid line ---- */

    ppv_display_init(&d, true, NULL);
    (void)ppv_display_process(&d, 0, NULL, 0, NULL, false, 0, line);
    EXPECT_STR(line, "ppv[R 0B avg - | W 0B avg -]\r");
    ppv_rates rr = rates(0, 0, 0, 0), wr = rates(1e9, 1.11e9, 1.12e9, 1.14e9);
    (void)ppv_display_process(&d, 0, &rr, MIB(3) * 1024, &wr, false, 0, line);
    EXPECT_STR(line, "ppv[R 0B avg 0B/s | W 3.00G avg 1.03G/s]\r");
    wr = rates(1e6, 1.5e6, 1.6e6, 2e6);
    (void)ppv_display_process(&d, 0, &rr, 4000000, &wr, true, 0, line);
    EXPECT_STR(line, "ppv[R 0B avg 0B/s | W 3.81M avg 1.43M/s]\n");

    return TEST_RESULT();
}
