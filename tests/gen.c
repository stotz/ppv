/* ppv_gen SIZE - write SIZE bytes of the test pattern to stdout. */
#include "pattern.h"

#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    uint64_t size = strtoull(argv[1], NULL, 10);
    set_binary(stdout);

    static unsigned char buf[65536];
    uint64_t pos = 0;
    while (pos < size) {
        size_t n = size - pos < sizeof buf ? (size_t)(size - pos) : sizeof buf;
        for (size_t k = 0; k < n; k++)
            buf[k] = pattern_at(pos + k);
        if (fwrite(buf, 1, n, stdout) != n)
            return 0; /* reader went away - fine for the "closed" test */
        pos += n;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}
