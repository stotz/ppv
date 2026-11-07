/* The live line: truncation, padding, line endings, terminal width. */
#include "display.h"
#include "test.h"

#include <stdio.h>

static ppv_display d;
static char line[PPV_LINE_BUF];

/* Renders and checks the whole output against expected. */
static void expect_line(const char *body, bool final, unsigned cols, const char *expected)
{
    size_t n = ppv_display_render(&d, body, final, cols, line);
    EXPECT_EQ(n, strlen(expected));
    EXPECT_EQ(strlen(line), n);
    EXPECT_STR(line, expected);
}

int main(void)
{
    /* Disabled: nothing at all */
    ppv_display_init(&d, false, "x");
    EXPECT_EQ(ppv_display_render(&d, "[1]", false, 0, line), 0);

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

    /* Name prefix; an empty name is no name */
    ppv_display_init(&d, true, "job");
    expect_line("[1]", false, 0, "job: [1]\r");
    ppv_display_init(&d, true, "");
    expect_line("[1]", false, 0, "[1]\r");

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

    /* The name counts towards the limit and is cut like the rest */
    ppv_display_init(&d, true, "abcdefghij");
    n = ppv_display_render(&d, "[1]", false, 8, line);
    EXPECT_EQ(n, 8);
    EXPECT_STR(line, "abcdefg\r");

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

    return TEST_RESULT();
}
