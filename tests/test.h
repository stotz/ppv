/* Minimal test helpers. */
#ifndef PPV_TEST_H
#define PPV_TEST_H

#include <stdio.h>
#include <string.h>

static int test_failures;

#define EXPECT_STR(actual, expected)                                                       \
    do {                                                                                   \
        const char *a_ = (actual), *e_ = (expected);                                       \
        if (strcmp(a_, e_) != 0) {                                                         \
            fprintf(stderr, "%s:%d: got '%s', expected '%s'\n", __FILE__, __LINE__, a_, e_); \
            test_failures++;                                                               \
        }                                                                                  \
    } while (0)

#define EXPECT_NEAR(actual, expected, tol)                                                  \
    do {                                                                                    \
        double a_ = (actual), e_ = (expected);                                              \
        if (a_ < e_ - (tol) || a_ > e_ + (tol)) {                                           \
            fprintf(stderr, "%s:%d: got %g, expected %g\n", __FILE__, __LINE__, a_, e_);    \
            test_failures++;                                                                \
        }                                                                                   \
    } while (0)

#define EXPECT_TRUE(cond)                                                                   \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #cond);             \
            test_failures++;                                                                \
        }                                                                                   \
    } while (0)

#define TEST_RESULT() (test_failures == 0 ? 0 : 1)

#endif
