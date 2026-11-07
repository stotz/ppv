/*
 * ppv - portable pipe viewer
 *
 * Platform layer: the only interface between the portable core and the
 * operating system. Implementations live in posix/ and win32/.
 */
#ifndef PPV_PLATFORM_H
#define PPV_PLATFORM_H

#include "compiler.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PPV_NS_PER_SEC UINT64_C(1000000000)

/* ------------------------------------------------------------------------ */
/* 64-bit atomic counter (relaxed ordering; used for statistics only)        */

/* MSVC: <stdatomic.h> only exists from VS 2022 17.5 with /experimental:c11atomics,
 * so always use the Interlocked intrinsics there. */
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
typedef struct ppv_atomic_u64 {
    volatile __int64 v;
} ppv_atomic_u64;
static inline void ppv_atomic_init(ppv_atomic_u64 *a, uint64_t v)
{
    a->v = (__int64)v;
}
static inline void ppv_atomic_add(ppv_atomic_u64 *a, uint64_t d)
{
    (void)_InterlockedExchangeAdd64(&a->v, (__int64)d);
}
static inline uint64_t ppv_atomic_load(ppv_atomic_u64 *a)
{
    return (uint64_t)_InterlockedCompareExchange64(&a->v, 0, 0);
}
#else
#  include <stdatomic.h>
typedef struct ppv_atomic_u64 {
    _Atomic uint64_t v;
} ppv_atomic_u64;
static inline void ppv_atomic_init(ppv_atomic_u64 *a, uint64_t v)
{
    atomic_init(&a->v, v);
}
static inline void ppv_atomic_add(ppv_atomic_u64 *a, uint64_t d)
{
    (void)atomic_fetch_add_explicit(&a->v, d, memory_order_relaxed);
}
static inline uint64_t ppv_atomic_load(ppv_atomic_u64 *a)
{
    return atomic_load_explicit(&a->v, memory_order_relaxed);
}
#endif

/* ------------------------------------------------------------------------ */
/* Process setup, interruption, time                                        */

/* Ignore SIGPIPE, install Ctrl-C / SIGINT / SIGTERM handling and make sure the
 * standard descriptors 0, 1 and 2 exist (a closed one is reopened on the null
 * device so that no descriptor created later can take its number). 0 on success. */
int ppv_platform_init(void);

/* True once the user asked us to stop (Ctrl-C, SIGINT, SIGTERM). */
bool ppv_interrupted(void);

/* Exit status for an interrupted run: 128 + signal, i.e. 130 after Ctrl-C /
 * SIGINT and 143 after SIGTERM. Only meaningful once ppv_interrupted() is true. */
int ppv_interrupt_exit_code(void);

/* Ends the process after an interruption. Flushes stdio; on POSIX the signal is
 * re-raised with its default disposition so that the parent sees a death by
 * signal (what a shell expects after Ctrl-C), elsewhere the process exits with
 * ppv_interrupt_exit_code(). Other threads are not joined. Never returns. */
PPV_NORETURN void ppv_exit_interrupted(void);

/* Monotonic clock in nanoseconds (arbitrary epoch). */
uint64_t ppv_now_ns(void);

void ppv_sleep_ns(uint64_t ns);

/* ------------------------------------------------------------------------ */
/* Status output on standard error                                          */

/* Console, terminal or Cygwin/MSYS pty (mintty, git-bash). */
bool ppv_stderr_is_tty(void);

/* False if we are a background job of the controlling terminal. */
bool ppv_stderr_in_foreground(void);

/* Width of the terminal behind stderr in columns; 0 if unknown. */
unsigned ppv_stderr_columns(void);

/* Unbuffered write of the whole buffer; errors are ignored. */
void ppv_stderr_write(const char *s, size_t len);

/* ------------------------------------------------------------------------ */
/* Data stream stdin -> stdout                                               */
/*                                                                          */
/* A transfer is split in two steps so the caller can measure separately   */
/* how long it waits for the producer (fill) and for the consumer (drain). */

typedef enum ppv_io_status {
    PPV_IO_OK,
    PPV_IO_EOF,
    PPV_IO_BROKEN, /* the reader of stdout has gone away */
    PPV_IO_ERROR
} ppv_io_status;

typedef struct ppv_stream ppv_stream;

ppv_stream *ppv_stream_open(size_t buffer_size, char *err, size_t err_cap);
/* Blocks until data is available on stdin and stages up to one buffer. */
ppv_io_status ppv_stream_fill(ppv_stream *s, size_t *got);
/* Blocks until all staged data has been written to stdout. */
ppv_io_status ppv_stream_drain(ppv_stream *s);
/* Human readable name of the transfer method in use, e.g. "splice". */
const char *ppv_stream_method(const ppv_stream *s);
/* Text of the last error reported by fill or drain. */
void ppv_stream_error(const ppv_stream *s, char *buf, size_t cap);
void ppv_stream_close(ppv_stream *s);

/* ------------------------------------------------------------------------ */
/* Threads and a latched one-shot event                                     */

typedef struct ppv_thread ppv_thread;
ppv_thread *ppv_thread_start(void (*fn)(void *), void *arg);
void ppv_thread_join(ppv_thread *t);

typedef struct ppv_event ppv_event;
ppv_event *ppv_event_create(void);
void ppv_event_set(ppv_event *e);
/* True if the event is set (now or within timeout_ns). May wake early. */
bool ppv_event_wait(ppv_event *e, uint64_t timeout_ns);
void ppv_event_destroy(ppv_event *e);

/* ------------------------------------------------------------------------ */
/* I/O counters of another process (--pid)                                  */

typedef struct ppv_proc ppv_proc;
ppv_proc *ppv_proc_open(uint64_t pid, char *err, size_t err_cap);
/* Total bytes read / written by the process (all files, pipes, sockets). */
bool ppv_proc_io(ppv_proc *p, uint64_t *read_bytes, uint64_t *write_bytes);
/* Waits up to timeout_ns; true if the process has exited. */
bool ppv_proc_wait_exit(ppv_proc *p, uint64_t timeout_ns);
void ppv_proc_close(ppv_proc *p);

#endif /* PPV_PLATFORM_H */
