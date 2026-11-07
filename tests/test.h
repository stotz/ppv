/* Minimal test helpers: EXPECT_* record a failure and continue. */
#ifndef PPV_TEST_H
#define PPV_TEST_H

#include <stdio.h>
#include <string.h>

static int test_failures;

#define TEST_FAIL(...)                                  \
    do {                                                \
        fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__);                   \
        fputc('\n', stderr);                            \
        test_failures++;                                \
    } while (0)

#define EXPECT_TRUE(cond)                    \
    do {                                     \
        if (!(cond))                         \
            TEST_FAIL("expected %s", #cond); \
    } while (0)

#define EXPECT_EQ(actual, expected)                                     \
    do {                                                                \
        long long a_ = (long long)(actual), e_ = (long long)(expected); \
        if (a_ != e_)                                                   \
            TEST_FAIL("%s: got %lld, expected %lld", #actual, a_, e_);  \
    } while (0)

#define EXPECT_STR(actual, expected)                      \
    do {                                                  \
        const char *a_ = (actual), *e_ = (expected);      \
        if (strcmp(a_, e_) != 0)                          \
            TEST_FAIL("got '%s', expected '%s'", a_, e_); \
    } while (0)

#define EXPECT_CONTAINS(haystack, needle)                \
    do {                                                 \
        const char *h_ = (haystack), *n_ = (needle);     \
        if (strstr(h_, n_) == NULL)                      \
            TEST_FAIL("'%s' not found in:\n%s", n_, h_); \
    } while (0)

#define EXPECT_NEAR(actual, expected, tol)            \
    do {                                              \
        double a_ = (actual), e_ = (expected);        \
        if (a_ < e_ - (tol) || a_ > e_ + (tol))       \
            TEST_FAIL("got %g, expected %g", a_, e_); \
    } while (0)

#define TEST_RESULT() (test_failures == 0 ? 0 : 1)

#endif
