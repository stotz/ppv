/* ppv - command line options. */
#ifndef PPV_OPTIONS_H
#define PPV_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Exit codes. An interrupted run exits with 128 + signal (see platform.h). */
enum {
    PPV_EXIT_OK = 0,
    PPV_EXIT_FAILURE = 1,
    PPV_EXIT_USAGE = 2,
    PPV_CONTINUE = -1 /* not an exit code: keep running */
};

#define PPV_INTERVAL_MIN_SEC 0.1
#define PPV_INTERVAL_MAX_SEC 60.0
#define PPV_BUFFER_MIN       ((size_t)4 * 1024)
#define PPV_BUFFER_MAX       ((size_t)256 * 1024 * 1024)
#define PPV_NAME_MAX         128 /* characters; keeps the live line and the report bounded */

typedef struct ppv_options {
    bool stats;
    bool quiet;
    bool force;
    bool wait;
    uint64_t interval_ns;
    size_t buffer_size;
    const char *name; /* NULL if not given or empty */
    bool watch_pid;
    uint64_t pid;
} ppv_options;

/* Parses argv. Help and version go to `out`, usage errors to `err` (both may
 * be stdout/stderr). Returns PPV_CONTINUE to run, otherwise the exit code
 * (0 after --help/--version, PPV_EXIT_USAGE after a usage error). */
int ppv_options_parse(ppv_options *o, int argc, char **argv, FILE *out, FILE *err);

#endif
