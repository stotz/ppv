/*
 * ppv - portable pipe viewer
 *
 *   cmd | ppv [-s] | cmd      copy stdin to stdout, show throughput
 *   ppv --pid PID [-s]        show the I/O throughput of a running process
 *
 * Threads: the transfer thread only moves data and bumps atomic counters;
 * the main thread samples them every PPV_SAMPLE_NS (100 ms) for the
 * statistics and draws the live line every -i interval.
 */
#include "display.h"
#include "meter.h"
#include "options.h"
#include "platform/platform.h"
#include "report.h"
#include "transfer.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Upper bound for how long we wait before re-checking for Ctrl-C. */
#define WAIT_SLICE_NS (UINT64_C(100000000))

typedef enum wake { WAKE_TICK, WAKE_DONE, WAKE_INTERRUPT } wake;

typedef bool (*done_fn)(void *ctx, uint64_t timeout_ns);

static wake wait_for_tick(uint64_t deadline, done_fn done, void *ctx)
{
    for (;;) {
        if (ppv_interrupted())
            return WAKE_INTERRUPT;
        uint64_t now = ppv_now_ns();
        if (now >= deadline)
            return WAKE_TICK;
        uint64_t slice = deadline - now;
        if (slice > WAIT_SLICE_NS)
            slice = WAIT_SLICE_NS;
        if (done(ctx, slice))
            return WAKE_DONE;
    }
}

static uint64_t next_deadline(uint64_t deadline, uint64_t now, uint64_t interval)
{
    deadline += interval;
    return deadline > now ? deadline : now + interval; /* skip missed ticks */
}

/* Drawing is suppressed for a background job (the terminal would stop us
 * with SIGTTOU, or garble the foreground job's output). */
static bool can_draw(const ppv_display *d)
{
    return d->enabled && ppv_stderr_in_foreground();
}

/* ------------------------------------------------------------------------ */
/* cmd | ppv | cmd                                                          */

static bool transfer_done(void *ctx, uint64_t timeout_ns)
{
    return ppv_event_wait(((ppv_transfer *)ctx)->done, timeout_ns);
}

static void draw_transfer(ppv_display *d, uint64_t bytes, const ppv_meter *m, bool final)
{
    char line[PPV_LINE_BUF];
    ppv_rates rates;
    if (!can_draw(d))
        return;
    bool have = ppv_window_rates(&m->window, &rates);
    size_t n =
        ppv_display_transfer(d, bytes, have ? &rates : NULL, final, ppv_stderr_columns(), line);
    ppv_stderr_write(line, n);
}

static int report_transfer_error(const ppv_transfer *t)
{
    switch (t->result) {
    case PPV_TRANSFER_OK:
    case PPV_TRANSFER_RUNNING:
        return PPV_EXIT_OK;
    case PPV_TRANSFER_SETUP_FAILED:
        fprintf(stderr, "ppv: %s\n", t->error);
        break;
    case PPV_TRANSFER_READ_FAILED:
        fprintf(stderr, "ppv: read error: %s\n", t->error);
        break;
    case PPV_TRANSFER_WRITE_FAILED:
        fprintf(stderr, "ppv: write error: %s\n", t->error);
        break;
    case PPV_TRANSFER_OUTPUT_CLOSED:
        fprintf(stderr, "ppv: output closed before the input ended\n");
        break;
    }
    return PPV_EXIT_FAILURE;
}

static int run_transfer(const ppv_options *o, bool live)
{
    static ppv_transfer xfer; /* static: shared with the thread, outlives an interrupted exit */
    static ppv_meter meter;

    if (!ppv_transfer_init(&xfer, o->buffer_size)) {
        fprintf(stderr, "ppv: cannot create synchronisation object\n");
        return PPV_EXIT_FAILURE;
    }

    uint64_t start = ppv_now_ns();
    ppv_thread *thread = ppv_thread_start(ppv_transfer_run, &xfer);
    if (thread == NULL) {
        fprintf(stderr, "ppv: cannot start transfer thread\n");
        ppv_transfer_destroy(&xfer);
        return PPV_EXIT_FAILURE;
    }

    ppv_meter_init(&meter, PPV_SAMPLE_NS, start, 0);
    ppv_display display;
    ppv_display_init(&display, live, o->name);

    uint64_t deadline = start + PPV_SAMPLE_NS;
    uint64_t next_draw = start + o->interval_ns;
    wake w;
    while ((w = wait_for_tick(deadline, transfer_done, &xfer)) == WAKE_TICK) {
        uint64_t now = ppv_now_ns();
        uint64_t bytes = ppv_atomic_load(&xfer.bytes);
        ppv_meter_sample(&meter, now, bytes);
        if (now >= next_draw) {
            if (!o->wait || bytes > 0)
                draw_transfer(&display, bytes, &meter, false);
            next_draw = next_deadline(next_draw, now, o->interval_ns);
        }
        deadline = next_deadline(deadline, now, PPV_SAMPLE_NS);
    }

    uint64_t end = ppv_now_ns();
    uint64_t bytes = ppv_atomic_load(&xfer.bytes);
    ppv_meter_finish(&meter, end, bytes);
    if (!o->wait || bytes > 0)
        draw_transfer(&display, bytes, &meter, true);

    bool interrupted = w == WAKE_INTERRUPT;
    int rc = PPV_EXIT_FAILURE;
    if (!interrupted) {
        ppv_thread_join(thread);
        rc = report_transfer_error(&xfer);
    }

    /* xfer.result is only safe to read after the join (not when interrupted). */
    bool setup_failed = !interrupted && xfer.result == PPV_TRANSFER_SETUP_FAILED;
    if (o->stats && !setup_failed) {
        ppv_rate_summary rates;
        ppv_meter_summary(&meter, &rates);
        ppv_transfer_report r = {
            .name = o->name,
            .interrupted = interrupted,
            .bytes = bytes,
            .elapsed_ns = end - start,
            .sample_ns = PPV_SAMPLE_NS,
            .in_read_ns = ppv_atomic_load(&xfer.blocked_in_ns),
            .in_write_ns = ppv_atomic_load(&xfer.blocked_out_ns),
            .ops = ppv_atomic_load(&xfer.ops),
            .method = interrupted ? NULL : xfer.method,
            .rates = &rates,
        };
        char text[PPV_REPORT_BUF];
        ppv_stderr_write(text, ppv_report_transfer(&r, text, sizeof text));
    }

    if (interrupted)
        ppv_exit_interrupted(); /* the transfer thread may be blocked in I/O forever */
    ppv_transfer_destroy(&xfer);
    return rc;
}

/* ------------------------------------------------------------------------ */
/* ppv --pid PID                                                            */

static bool process_done(void *ctx, uint64_t timeout_ns)
{
    return ppv_proc_wait_exit((ppv_proc *)ctx, timeout_ns);
}

static void draw_process(ppv_display *d, uint64_t r, const ppv_meter *mr, uint64_t w,
                         const ppv_meter *mw, bool final)
{
    char line[PPV_LINE_BUF];
    ppv_rates rr, wr;
    if (!can_draw(d))
        return;
    bool have_r = ppv_window_rates(&mr->window, &rr);
    bool have_w = ppv_window_rates(&mw->window, &wr);
    size_t n = ppv_display_process(d, r, have_r ? &rr : NULL, w, have_w ? &wr : NULL, final,
                                   ppv_stderr_columns(), line);
    ppv_stderr_write(line, n);
}

static int run_process(const ppv_options *o, bool live)
{
    static ppv_meter mread, mwrite;
    char err[256];

    ppv_proc *p = ppv_proc_open(o->pid, err, sizeof err);
    if (p == NULL) {
        fprintf(stderr, "ppv: pid %" PRIu64 ": %s\n", o->pid, err);
        return PPV_EXIT_FAILURE;
    }

    uint64_t r0, w0;
    if (!ppv_proc_io(p, &r0, &w0)) {
        fprintf(stderr, "ppv: pid %" PRIu64 ": cannot read I/O counters\n", o->pid);
        ppv_proc_close(p);
        return PPV_EXIT_FAILURE;
    }

    uint64_t start = ppv_now_ns();
    ppv_meter_init(&mread, PPV_SAMPLE_NS, start, 0);
    ppv_meter_init(&mwrite, PPV_SAMPLE_NS, start, 0);
    ppv_display display;
    ppv_display_init(&display, live, o->name);

    uint64_t r = 0, w = 0, cr, cw;
    uint64_t deadline = start + PPV_SAMPLE_NS;
    uint64_t next_draw = start + o->interval_ns;
    wake wk;
    while ((wk = wait_for_tick(deadline, process_done, p)) == WAKE_TICK) {
        uint64_t now = ppv_now_ns();
        if (ppv_proc_io(p, &cr, &cw)) {
            r = cr > r0 ? cr - r0 : 0;
            w = cw > w0 ? cw - w0 : 0;
        }
        ppv_meter_sample(&mread, now, r);
        ppv_meter_sample(&mwrite, now, w);
        if (now >= next_draw) {
            if (!o->wait || r > 0 || w > 0)
                draw_process(&display, r, &mread, w, &mwrite, false);
            next_draw = next_deadline(next_draw, now, o->interval_ns);
        }
        deadline = next_deadline(deadline, now, PPV_SAMPLE_NS);
    }

    /* Windows still delivers the final counters of an exited process; on
     * Linux /proc is gone by then and the last partial interval is lost. A
     * stale reading must not enter the statistics as a 0 B/s sample. */
    uint64_t end = ppv_now_ns();
    if (ppv_proc_io(p, &cr, &cw)) {
        r = cr > r0 ? cr - r0 : r;
        w = cw > w0 ? cw - w0 : w;
        ppv_meter_finish(&mread, end, r);
        ppv_meter_finish(&mwrite, end, w);
    }
    if (!o->wait || r > 0 || w > 0)
        draw_process(&display, r, &mread, w, &mwrite, true);
    ppv_proc_close(p);

    bool interrupted = wk == WAKE_INTERRUPT;
    if (o->stats) {
        ppv_rate_summary rrates, wrates;
        ppv_meter_summary(&mread, &rrates);
        ppv_meter_summary(&mwrite, &wrates);
        ppv_process_report rep = {
            .name = o->name,
            .interrupted = interrupted,
            .pid = o->pid,
            .elapsed_ns = end - start,
            .sample_ns = PPV_SAMPLE_NS,
            .read_bytes = r,
            .write_bytes = w,
            .read_rates = &rrates,
            .write_rates = &wrates,
        };
        char text[PPV_REPORT_BUF];
        ppv_stderr_write(text, ppv_report_process(&rep, text, sizeof text));
    }
    if (interrupted)
        ppv_exit_interrupted();
    return PPV_EXIT_OK;
}

/* ------------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    ppv_options opt;
    int rc = ppv_options_parse(&opt, argc, argv, stdout, stderr);
    if (rc != PPV_CONTINUE)
        return rc;

    if (ppv_platform_init() != 0) {
        fprintf(stderr, "ppv: platform initialisation failed\n");
        return PPV_EXIT_FAILURE;
    }

    bool live = !opt.quiet && (opt.force || ppv_stderr_is_tty());
    return opt.watch_pid ? run_process(&opt, live) : run_transfer(&opt, live);
}
