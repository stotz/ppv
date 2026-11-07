#include "options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PPV_VERSION
#  define PPV_VERSION "dev"
#endif

static const char help_text[] =
    "Usage: cmd | ppv [OPTION]... | cmd\n"
    "       ppv --pid PID [OPTION]...\n"
    "Copy standard input to standard output and show the throughput on standard error.\n"
    "\n"
    "Live line: [total, min, avg, max] - rates over the last minute.\n"
    "\n"
    "  -s, --stats             print statistics at the end\n"
    "  -q, --quiet             no live line\n"
    "  -f, --force             live line even if stderr is not a terminal\n"
    "  -W, --wait              show nothing until the first byte has arrived\n"
    "  -i, --interval SEC      update interval, 0.1 to 60 seconds (default 1)\n"
    "  -N, --name NAME         prefix the output with NAME (at most 128 characters)\n"
    "  -B, --buffer-size SIZE  transfer buffer, 4K to 256M (default 1M)\n"
    "  -p, --pid PID           watch the I/O of process PID instead of copying\n"
    "  -h, --help              show this help\n"
    "  -V, --version           show the version\n"
    "\n"
    "With --pid the line shows bytes read (R) and written (W) by the process.\n"
    "In git-bash use the Windows PID (ps -W, or cat /proc/PID/winpid).\n"
    "\n"
    "Exit status: 0 ok, 1 error or output closed early, 2 usage error,\n"
    "             130 interrupted (Ctrl-C), 143 terminated (SIGTERM).\n";

typedef enum opt_id {
    OPT_STATS,
    OPT_QUIET,
    OPT_FORCE,
    OPT_WAIT,
    OPT_INTERVAL,
    OPT_NAME,
    OPT_BUFFER,
    OPT_PID,
    OPT_HELP,
    OPT_VERSION
} opt_id;

typedef struct opt_def {
    const char *long_name;
    opt_id id;
    char short_name;
    bool has_arg;
} opt_def;

/* clang-format off */
static const opt_def defs[] = {
    {"stats",       OPT_STATS,    's', false},
    {"quiet",       OPT_QUIET,    'q', false},
    {"force",       OPT_FORCE,    'f', false},
    {"wait",        OPT_WAIT,     'W', false},
    {"interval",    OPT_INTERVAL, 'i', true},
    {"name",        OPT_NAME,     'N', true},
    {"buffer-size", OPT_BUFFER,   'B', true},
    {"pid",         OPT_PID,      'p', true},
    {"help",        OPT_HELP,     'h', false},
    {"version",     OPT_VERSION,  'V', false},
};
/* clang-format on */

#define NDEFS (sizeof defs / sizeof defs[0])

typedef struct parser {
    ppv_options *o;
    int argc;
    char **argv;
    int i;
    FILE *out;
    FILE *err;
} parser;

static int usage_error(const parser *p, const char *what, const char *arg)
{
    fprintf(p->err, "ppv: %s '%s'\nTry 'ppv --help' for more information.\n", what, arg);
    return PPV_EXIT_USAGE;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* "1", "0.5", ".25": plain decimal seconds only (strtod would also take
 * "1e0", "0x1p-1", "inf" and locale specific spellings). */
static bool parse_seconds(const char *s, uint64_t *ns)
{
    bool digits = false, dot = false;
    for (const char *p = s; *p != '\0'; p++) {
        if (is_digit(*p))
            digits = true;
        else if (*p == '.' && !dot)
            dot = true;
        else
            return false;
    }
    if (!digits)
        return false;
    double v = strtod(s, NULL);
    if (!(v >= PPV_INTERVAL_MIN_SEC && v <= PPV_INTERVAL_MAX_SEC))
        return false;
    *ns = (uint64_t)(v * 1e9 + 0.5);
    return true;
}

static bool parse_size(const char *s, size_t *out)
{
    if (!is_digit(s[0]))
        return false;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0)
        return false;

    unsigned long long mul = 1;
    switch (*end) {
    case '\0':
        break;
    case 'k':
    case 'K':
        mul = 1024ull;
        end++;
        break;
    case 'm':
    case 'M':
        mul = 1024ull * 1024;
        end++;
        break;
    case 'g':
    case 'G':
        mul = 1024ull * 1024 * 1024;
        end++;
        break;
    default:
        return false;
    }
    if (*end != '\0' || v > PPV_BUFFER_MAX / mul)
        return false;
    v *= mul;
    if (v < PPV_BUFFER_MIN)
        return false;
    *out = (size_t)v;
    return true;
}

static bool parse_pid(const char *s, uint64_t *pid)
{
    if (!is_digit(s[0]))
        return false;
    for (const char *p = s; *p != '\0'; p++)
        if (!is_digit(*p))
            return false;
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 10);
    if (errno != 0 || v == 0)
        return false;
    *pid = (uint64_t)v;
    return true;
}

/* val is the option argument (NULL for flags), spelled the option as typed. */
static int apply(parser *p, const opt_def *d, const char *val, const char *spelled)
{
    ppv_options *o = p->o;
    if (d->has_arg && val == NULL)
        return usage_error(p, "missing argument for", spelled);
    const char *arg = val != NULL ? val : ""; /* flags: never looked at */

    switch (d->id) {
    case OPT_STATS:
        o->stats = true;
        break;
    case OPT_QUIET:
        o->quiet = true;
        break;
    case OPT_FORCE:
        o->force = true;
        break;
    case OPT_WAIT:
        o->wait = true;
        break;
    case OPT_NAME:
        if (strlen(arg) > PPV_NAME_MAX)
            return usage_error(p, "name too long (at most 128 characters) for", spelled);
        o->name = arg[0] != '\0' ? arg : NULL;
        break;
    case OPT_INTERVAL:
        if (!parse_seconds(arg, &o->interval_ns))
            return usage_error(p, "invalid interval (0.1 to 60 seconds) for", spelled);
        break;
    case OPT_BUFFER:
        if (!parse_size(arg, &o->buffer_size))
            return usage_error(p, "invalid buffer size (4K to 256M) for", spelled);
        break;
    case OPT_PID:
        if (!parse_pid(arg, &o->pid))
            return usage_error(p, "invalid process id for", spelled);
        o->watch_pid = true;
        break;
    case OPT_HELP:
        fputs(help_text, p->out);
        return PPV_EXIT_OK;
    case OPT_VERSION:
        fputs("ppv " PPV_VERSION "\n", p->out);
        return PPV_EXIT_OK;
    }
    return PPV_CONTINUE;
}

/* The next argv element as option argument, or NULL if there is none. */
static const char *take_next(parser *p)
{
    return p->i + 1 < p->argc ? p->argv[++p->i] : NULL;
}

static int parse_long(parser *p, const char *arg)
{
    const char *name = arg + 2;
    const char *eq = strchr(name, '=');
    size_t len = eq != NULL ? (size_t)(eq - name) : strlen(name);

    for (size_t k = 0; k < NDEFS; k++) {
        const opt_def *d = &defs[k];
        if (strlen(d->long_name) != len || strncmp(d->long_name, name, len) != 0)
            continue;
        if (!d->has_arg && eq != NULL)
            return usage_error(p, "option takes no argument:", arg);
        const char *val = NULL;
        if (d->has_arg)
            val = eq != NULL ? eq + 1 : take_next(p);
        return apply(p, d, val, arg);
    }
    return usage_error(p, "unknown option", arg);
}

static int parse_short(parser *p, const char *arg)
{
    for (const char *c = arg + 1; *c != '\0'; c++) {
        const opt_def *d = NULL;
        for (size_t k = 0; k < NDEFS; k++)
            if (defs[k].short_name == *c)
                d = &defs[k];

        char spelled[3] = {'-', *c, '\0'};
        if (d == NULL)
            return usage_error(p, "unknown option", spelled);

        const char *val = NULL;
        if (d->has_arg)
            val = c[1] != '\0' ? c + 1 /* -i0.5 */ : take_next(p);
        int rc = apply(p, d, val, spelled);
        if (rc != PPV_CONTINUE || d->has_arg)
            return rc;
    }
    return PPV_CONTINUE;
}

int ppv_options_parse(ppv_options *o, int argc, char **argv, FILE *out, FILE *err)
{
    parser p = {.o = o, .argc = argc, .argv = argv, .i = 0, .out = out, .err = err};

    memset(o, 0, sizeof *o);
    o->interval_ns = UINT64_C(1000000000);
    o->buffer_size = (size_t)1024 * 1024;

    for (p.i = 1; p.i < argc; p.i++) {
        const char *arg = argv[p.i];
        int rc;
        if (strcmp(arg, "--") == 0) {
            if (p.i + 1 < argc)
                return usage_error(&p, "unexpected argument (ppv only reads stdin)", argv[p.i + 1]);
            break;
        } else if (strncmp(arg, "--", 2) == 0) {
            rc = parse_long(&p, arg);
        } else if (arg[0] == '-' && arg[1] != '\0') {
            rc = parse_short(&p, arg);
        } else {
            return usage_error(&p, "unexpected argument (ppv only reads stdin)", arg);
        }
        if (rc != PPV_CONTINUE)
            return rc;
    }
    return PPV_CONTINUE;
}
