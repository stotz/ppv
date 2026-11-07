#include "transfer.h"

#include <string.h>

bool ppv_transfer_init(ppv_transfer *t, size_t buffer_size)
{
    memset(t, 0, sizeof *t);
    t->buffer_size = buffer_size;
    ppv_atomic_init(&t->bytes, 0);
    ppv_atomic_init(&t->ops, 0);
    ppv_atomic_init(&t->blocked_in_ns, 0);
    ppv_atomic_init(&t->blocked_out_ns, 0);
    t->result = PPV_TRANSFER_RUNNING;
    t->method = "-";
    t->done = ppv_event_create();
    return t->done != NULL;
}

void ppv_transfer_run(void *arg)
{
    ppv_transfer *t = arg;
    ppv_stream *s = ppv_stream_open(t->buffer_size, t->error, sizeof t->error);
    if (s == NULL) {
        t->result = PPV_TRANSFER_SETUP_FAILED;
        ppv_event_set(t->done);
        return;
    }

    ppv_transfer_result result = PPV_TRANSFER_OK;
    for (;;) {
        size_t n = 0;
        uint64_t t0 = ppv_now_ns();
        ppv_io_status st = ppv_stream_fill(s, &n);
        uint64_t t1 = ppv_now_ns();
        ppv_atomic_add(&t->blocked_in_ns, t1 - t0);

        if (st == PPV_IO_EOF)
            break;
        if (st != PPV_IO_OK) {
            result = PPV_TRANSFER_READ_FAILED;
            break;
        }

        st = ppv_stream_drain(s);
        ppv_atomic_add(&t->blocked_out_ns, ppv_now_ns() - t1);
        if (st != PPV_IO_OK) {
            result = st == PPV_IO_BROKEN ? PPV_TRANSFER_OUTPUT_CLOSED : PPV_TRANSFER_WRITE_FAILED;
            break;
        }

        ppv_atomic_add(&t->bytes, n);
        ppv_atomic_add(&t->ops, 1);
    }

    if (result != PPV_TRANSFER_OK)
        ppv_stream_error(s, t->error, sizeof t->error);
    t->method = ppv_stream_method(s);
    ppv_stream_close(s);

    t->result = result;
    ppv_event_set(t->done); /* publishes result/method/error to the main thread */
}

void ppv_transfer_destroy(ppv_transfer *t)
{
    ppv_event_destroy(t->done);
    t->done = NULL;
}
