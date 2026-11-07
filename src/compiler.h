/* ppv - compiler specific helpers, usable from the portable core. */
#ifndef PPV_COMPILER_H
#define PPV_COMPILER_H

/* printf-style format checking (MinGW needs the ms/gnu-aware variant). */
#if defined(__MINGW_PRINTF_FORMAT)
#  define PPV_PRINTF(fmt, args) __attribute__((format(__MINGW_PRINTF_FORMAT, fmt, args)))
#elif defined(__GNUC__) || defined(__clang__)
#  define PPV_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#  define PPV_PRINTF(fmt, args)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define PPV_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#  define PPV_NORETURN __declspec(noreturn)
#else
#  define PPV_NORETURN
#endif

#endif
