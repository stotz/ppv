/*
 * ppv_check SIZE         - verify stdin is exactly SIZE bytes of the pattern
 * ppv_check SIZE --stop  - verify the first SIZE bytes, then exit (closes the pipe)
 */
#include "pattern.h"

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    uint64_t size = strtoull(argv[1], NULL, 10);
    int stop = argc > 2 && strcmp(argv[2], "--stop") == 0;
    set_binary(stdin);

    static unsigned char buf[65536];
    uint64_t pos = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, stdin)) > 0) {
        for (size_t k = 0; k < n; k++) {
            if (stop && pos + k >= size)
                return 0;
            if (pos + k >= size) {
                fprintf(stderr, "check: more than %llu bytes\n", (unsigned long long)size);
                return 1;
            }
            if (buf[k] != pattern_at(pos + k)) {
                fprintf(stderr, "check: mismatch at byte %llu\n", (unsigned long long)(pos + k));
                return 1;
            }
        }
        pos += n;
        if (stop && pos >= size)
            return 0;
    }
    if (pos != size) {
        fprintf(stderr, "check: got %llu bytes, expected %llu\n", (unsigned long long)pos,
                (unsigned long long)size);
        return 1;
    }
    return 0;
}
