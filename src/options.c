#include "options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PPV_VERSION
#  define PPV_VERSION "dev"
#endif

#define MIN_BUFFER ((size_t)4 * 1024)
#define MAX_BUFFER ((size_t)256 * 1024 * 1024)

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
    "  -N, --name NAME         prefix the output with NAME\n"
    "  -B, --buffer-size SIZE  transfer buffer, 4K to 256M (default 1M)\n"
    "  -p, --pid PID           watch the I/O of process PID instead of copying\n"
    "  -h, --help              show this help\n"
    "  -V, --version           show the version\n"
    "\n"
    "With --pid the line shows bytes read (R) and written (W) by the process.\n"
    "In git-bash use the Windows PID (ps -W, or cat /proc/PID/winpid).\n"
    "\n"
    "Exit status: 0 ok, 1 error or output closed early, 2 usage, 130 interrupted.\n";

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
    char short_name;
    const char *long_name;
    bool has_arg;
    opt_id id;
} opt_def;

static const opt_def defs[] = {
    {'s', "stats", false, OPT_STATS},
    {'q', "quiet", false, OPT_QUIET},
    {'f', "force", false, OPT_FORCE},
    {'W', "wait", false, OPT_WAIT},
    {'i', "interval", true, OPT_INTERVAL},
    {'N', "name", true, OPT_NAME},
    {'B', "buffer-size", true, OPT_BUFFER},
    {'p', "pid", true, OPT_PID},
    {'h', "help", false, OPT_HELP},
    {'V', "version", false, OPT_VERSION},
};

#define NDEFS (sizeof defs / sizeof defs[0])

static int usage_error(const char *what, const char *arg)
{
    fprintf(stderr, "ppv: %s '%s'\nTry 'ppv --help' for more information.\n", what, arg);
    return PPV_EXIT_USAGE;
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_seconds(const char *s, uint64_t *ns)
{
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || *end != '\0' || errno != 0 || !(v >= 0.1 && v <= 60.0))
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
    case '\0': break;
    case 'k': case 'K': mul = 1024ull; end++; break;
    case 'm': case 'M': mul = 1024ull * 1024; end++; break;
    case 'g': case 'G': mul = 1024ull * 1024 * 1024; end++; break;
    default: return false;
    }
    if (*end != '\0' || v > MAX_BUFFER / mul)
        return false;
    v *= mul;
    if (v < MIN_BUFFER)
        return false;
    *out = (size_t)v;
    return true;
}

static bool parse_pid(const char *s, uint64_t *pid)
{
    for (const char *p = s; *p != '\0'; p++)
        if (!is_digit(*p))
            return false;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || errno != 0 || v == 0)
        return false;
    *pid = (uint64_t)v;
    return true;
}

static int apply(ppv_options *o, const opt_def *d, const char *val, const char *spelled)
{
    switch (d->id) {
    case OPT_STATS: o->stats = true; break;
    case OPT_QUIET: o->quiet = true; break;
    case OPT_FORCE: o->force = true; break;
    case OPT_WAIT: o->wait = true; break;
    case OPT_NAME: o->name = val; break;
    case OPT_INTERVAL:
        if (!parse_seconds(val, &o->interval_ns))
            return usage_error("invalid interval (0.1 to 60 seconds) for", spelled);
        break;
    case OPT_BUFFER:
        if (!parse_size(val, &o->buffer_size))
            return usage_error("invalid buffer size (4K to 256M) for", spelled);
        break;
    case OPT_PID:
        if (!parse_pid(val, &o->pid))
            return usage_error("invalid process id for", spelled);
        o->watch_pid = true;
        break;
    case OPT_HELP:
        fputs(help_text, stdout);
        return PPV_EXIT_OK;
    case OPT_VERSION:
        puts("ppv " PPV_VERSION);
        return PPV_EXIT_OK;
    }
    return PPV_CONTINUE;
}

static int parse_long(ppv_options *o, const char *arg, int argc, char **argv, int *i)
{
    const char *name = arg + 2;
    const char *eq = strchr(name, '=');
    size_t len = eq != NULL ? (size_t)(eq - name) : strlen(name);

    for (size_t k = 0; k < NDEFS; k++) {
        const opt_def *d = &defs[k];
        if (strlen(d->long_name) != len || strncmp(d->long_name, name, len) != 0)
            continue;
        const char *val = NULL;
        if (d->has_arg) {
            if (eq != NULL)
                val = eq + 1;
            else if (*i + 1 < argc)
                val = argv[++*i];
            else
                return usage_error("missing argument for", arg);
        } else if (eq != NULL) {
            return usage_error("option takes no argument:", arg);
        }
        return apply(o, d, val, arg);
    }
    return usage_error("unknown option", arg);
}

static int parse_short(ppv_options *o, const char *arg, int argc, char **argv, int *i)
{
    for (const char *p = arg + 1; *p != '\0'; p++) {
        const opt_def *d = NULL;
        for (size_t k = 0; k < NDEFS; k++)
            if (defs[k].short_name == *p)
                d = &defs[k];

        char spelled[3] = {'-', *p, '\0'};
        if (d == NULL)
            return usage_error("unknown option", spelled);

        const char *val = NULL;
        if (d->has_arg) {
            if (p[1] != '\0')
                val = p + 1; /* -i0.5 */
            else if (*i + 1 < argc)
                val = argv[++*i];
            else
                return usage_error("missing argument for", spelled);
        }
        int rc = apply(o, d, val, spelled);
        if (rc != PPV_CONTINUE || d->has_arg)
            return rc;
    }
    return PPV_CONTINUE;
}

int ppv_options_parse(ppv_options *o, int argc, char **argv)
{
    memset(o, 0, sizeof *o);
    o->interval_ns = UINT64_C(1000000000);
    o->buffer_size = (size_t)1024 * 1024;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        int rc;
        if (strcmp(arg, "--") == 0) {
            if (i + 1 < argc)
                return usage_error("unexpected argument (ppv only reads stdin)", argv[i + 1]);
            break;
        } else if (strncmp(arg, "--", 2) == 0) {
            rc = parse_long(o, arg, argc, argv, &i);
        } else if (arg[0] == '-' && arg[1] != '\0') {
            rc = parse_short(o, arg, argc, argv, &i);
        } else {
            return usage_error("unexpected argument (ppv only reads stdin)", arg);
        }
        if (rc != PPV_CONTINUE)
            return rc;
    }
    return PPV_CONTINUE;
}
