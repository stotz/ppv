/* ppv - command line options. */
#ifndef PPV_OPTIONS_H
#define PPV_OPTIONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    PPV_EXIT_OK = 0,
    PPV_EXIT_FAILURE = 1,
    PPV_EXIT_USAGE = 2,
    PPV_EXIT_INTERRUPTED = 130,
    PPV_CONTINUE = -1
};

typedef struct ppv_options {
    bool stats;
    bool quiet;
    bool force;
    bool wait;
    uint64_t interval_ns;
    size_t buffer_size;
    const char *name;
    bool watch_pid;
    uint64_t pid;
} ppv_options;

/* PPV_CONTINUE to run, otherwise the exit code (help, version, usage error). */
int ppv_options_parse(ppv_options *o, int argc, char **argv);

#endif
