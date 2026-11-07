/* Command line parser. */
#include "options.h"
#include "test.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#define S UINT64_C(1000000000)

static ppv_options o;
static char out_text[4096];
static char err_text[4096];

/* Reads a whole temporary file into buf (NUL terminated). */
static void slurp(FILE *f, char *buf, size_t cap)
{
    rewind(f);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = '\0';
}

/* Parses the NULL terminated argument list (argv[0] is supplied here). The
 * arguments are copied because argv must be writable char pointers. */
static int parse(const char *a1, ...)
{
    static char store[16][PPV_NAME_MAX + 16];
    char *argv[16];
    int argc = 0;

    snprintf(store[argc], sizeof store[argc], "ppv");
    argv[argc] = store[argc];
    argc++;

    va_list ap;
    va_start(ap, a1);
    /* NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) false positive: va_start is above */
    for (const char *a = a1; a != NULL && argc < 15; a = va_arg(ap, const char *)) {
        snprintf(store[argc], sizeof store[argc], "%s", a);
        argv[argc] = store[argc];
        argc++;
    }
    va_end(ap);
    argv[argc] = NULL;

    FILE *out = tmpfile(), *err = tmpfile();
    if (out == NULL || err == NULL) {
        fprintf(stderr, "tmpfile failed\n");
        exit(1);
    }
    int rc = ppv_options_parse(&o, argc, argv, out, err);
    slurp(out, out_text, sizeof out_text);
    slurp(err, err_text, sizeof err_text);
    fclose(out);
    fclose(err);
    return rc;
}

int main(void)
{
    /* Defaults */
    EXPECT_EQ(parse(NULL), PPV_CONTINUE);
    EXPECT_TRUE(!o.stats && !o.quiet && !o.force && !o.wait && !o.watch_pid);
    EXPECT_EQ(o.interval_ns, S);
    EXPECT_EQ(o.buffer_size, 1024 * 1024);
    EXPECT_TRUE(o.name == NULL);
    EXPECT_STR(err_text, "");

    /* Flags, bundled and separate; long forms */
    EXPECT_EQ(parse("-sqfW", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.stats && o.quiet && o.force && o.wait);
    EXPECT_EQ(parse("--stats", "--quiet", "--force", "--wait", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.stats && o.quiet && o.force && o.wait);

    /* Interval: attached, separate, long with and without '=' */
    EXPECT_EQ(parse("-i0.5", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.interval_ns, S / 2);
    EXPECT_EQ(parse("-i", "2", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.interval_ns, 2 * S);
    EXPECT_EQ(parse("--interval=.25", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.interval_ns, S / 4);
    EXPECT_EQ(parse("--interval", "60", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.interval_ns, 60 * S);
    EXPECT_EQ(parse("-si0.1", NULL), PPV_CONTINUE); /* flag then option with value */
    EXPECT_TRUE(o.stats);
    EXPECT_EQ(o.interval_ns, S / 10);

    /* Interval: out of range or not a plain decimal */
    EXPECT_EQ(parse("-i", "0", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "invalid interval");
    EXPECT_EQ(parse("-i", "0.05", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "61", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "1e0", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "0x1p-1", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "nan", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "inf", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "1,5", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", ".", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", "", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", " 1", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-i", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "missing argument for '-i'");
    EXPECT_EQ(parse("--interval", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "missing argument for '--interval'");

    /* Buffer size with units, limits inclusive */
    EXPECT_EQ(parse("-B", "4096", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.buffer_size, 4096);
    EXPECT_EQ(parse("-B4k", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.buffer_size, 4096);
    EXPECT_EQ(parse("--buffer-size=64K", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.buffer_size, 64 * 1024);
    EXPECT_EQ(parse("-B", "2m", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.buffer_size, 2 * 1024 * 1024);
    EXPECT_EQ(parse("-B", "256M", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.buffer_size, (size_t)256 * 1024 * 1024);
    EXPECT_EQ(parse("-B", "4095", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "invalid buffer size");
    EXPECT_EQ(parse("-B", "257M", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "1G", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "0", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "-1", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "64KB", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "K", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "99999999999999999999", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-B", "", NULL), PPV_EXIT_USAGE);

    /* Name: empty means none; a value starting with '-' is taken literally */
    EXPECT_EQ(parse("-N", "job", NULL), PPV_CONTINUE);
    EXPECT_STR(o.name, "job");
    EXPECT_EQ(parse("-Njob", "-s", NULL), PPV_CONTINUE);
    EXPECT_STR(o.name, "job");
    EXPECT_TRUE(o.stats);
    EXPECT_EQ(parse("--name=", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.name == NULL);
    EXPECT_EQ(parse("--name", "", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.name == NULL);
    EXPECT_EQ(parse("-N", "-s", NULL), PPV_CONTINUE);
    EXPECT_STR(o.name, "-s");
    EXPECT_TRUE(!o.stats);
    EXPECT_EQ(parse("--name=a=b", NULL), PPV_CONTINUE);
    EXPECT_STR(o.name, "a=b");
    {
        static char longest[PPV_NAME_MAX + 2];
        memset(longest, 'n', PPV_NAME_MAX);
        longest[PPV_NAME_MAX] = '\0';
        EXPECT_EQ(parse("-N", longest, NULL), PPV_CONTINUE); /* exactly the limit */
        EXPECT_EQ(strlen(o.name), PPV_NAME_MAX);
        longest[PPV_NAME_MAX] = 'n';
        longest[PPV_NAME_MAX + 1] = '\0';
        EXPECT_EQ(parse("-N", longest, NULL), PPV_EXIT_USAGE); /* one more */
        EXPECT_CONTAINS(err_text, "name too long");
    }

    /* PID */
    EXPECT_EQ(parse("-p", "1234", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.watch_pid);
    EXPECT_EQ(o.pid, 1234);
    EXPECT_EQ(parse("--pid=42", "-s", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.pid, 42);
    EXPECT_EQ(parse("--pid", "0", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "invalid process id");
    EXPECT_EQ(parse("--pid", "abc", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("--pid", "12a", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("--pid", "-1", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("--pid", "", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("--pid", "99999999999999999999999", NULL), PPV_EXIT_USAGE);

    /* Unknown options, stray arguments, "--" */
    EXPECT_EQ(parse("--bogus", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "unknown option '--bogus'");
    EXPECT_CONTAINS(err_text, "Try 'ppv --help'");
    EXPECT_EQ(parse("-x", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "unknown option '-x'");
    EXPECT_EQ(parse("-sx", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "unknown option '-x'");
    EXPECT_EQ(parse("--stats=1", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "takes no argument");
    EXPECT_EQ(parse("--stat", NULL), PPV_EXIT_USAGE); /* no abbreviations */
    EXPECT_EQ(parse("file", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "unexpected argument");
    EXPECT_EQ(parse("-", NULL), PPV_EXIT_USAGE);
    EXPECT_EQ(parse("-s", "--", NULL), PPV_CONTINUE);
    EXPECT_TRUE(o.stats);
    EXPECT_EQ(parse("--", "file", NULL), PPV_EXIT_USAGE);
    EXPECT_CONTAINS(err_text, "'file'");

    /* Help and version go to `out`, return 0 and stop parsing */
    EXPECT_EQ(parse("-h", NULL), PPV_EXIT_OK);
    EXPECT_CONTAINS(out_text, "Usage: cmd | ppv");
    EXPECT_CONTAINS(out_text, "--buffer-size SIZE");
    EXPECT_STR(err_text, "");
    EXPECT_EQ(parse("--help", "--bogus", NULL), PPV_EXIT_OK);
    EXPECT_EQ(parse("-V", NULL), PPV_EXIT_OK);
    EXPECT_CONTAINS(out_text, "ppv ");
    EXPECT_EQ(parse("--version", NULL), PPV_EXIT_OK);
    EXPECT_TRUE(strncmp(out_text, "ppv ", 4) == 0);
    EXPECT_TRUE(out_text[strlen(out_text) - 1] == '\n');

    /* Later options win */
    EXPECT_EQ(parse("-i", "1", "-i", "3", "-B", "8K", "-B", "16K", NULL), PPV_CONTINUE);
    EXPECT_EQ(o.interval_ns, 3 * S);
    EXPECT_EQ(o.buffer_size, 16 * 1024);

    return TEST_RESULT();
}
