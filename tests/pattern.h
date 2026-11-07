/* Deterministic test data covering all byte values incl. CR, LF and ^Z. */
#ifndef PPV_TEST_PATTERN_H
#define PPV_TEST_PATTERN_H

#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
static void set_binary(FILE *f)
{
    (void)_setmode(_fileno(f), _O_BINARY);
}
#else
static void set_binary(FILE *f)
{
    (void)f;
}
#endif

static unsigned char pattern_at(uint64_t i)
{
    return (unsigned char)((i * 131u) ^ (i >> 11));
}

#endif
