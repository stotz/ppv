/* ppv - the copy loop stdin -> stdout, run in its own thread. */
#ifndef PPV_TRANSFER_H
#define PPV_TRANSFER_H

#include "platform/platform.h"

typedef enum ppv_transfer_result {
    PPV_TRANSFER_RUNNING,
    PPV_TRANSFER_OK,
    PPV_TRANSFER_SETUP_FAILED,
    PPV_TRANSFER_READ_FAILED,
    PPV_TRANSFER_WRITE_FAILED,
    PPV_TRANSFER_OUTPUT_CLOSED
} ppv_transfer_result;

typedef struct ppv_transfer {
    size_t buffer_size;

    /* Updated live by the transfer thread. */
    ppv_atomic_u64 bytes;
    ppv_atomic_u64 ops;
    ppv_atomic_u64 blocked_in_ns;  /* waiting for the producer */
    ppv_atomic_u64 blocked_out_ns; /* waiting for the consumer */

    ppv_event *done;

    /* Valid once `done` is set. */
    ppv_transfer_result result;
    const char *method;
    char error[256];
} ppv_transfer;

bool ppv_transfer_init(ppv_transfer *t, size_t buffer_size);
void ppv_transfer_run(void *arg); /* thread entry, arg = ppv_transfer* */
void ppv_transfer_destroy(ppv_transfer *t);

#endif
