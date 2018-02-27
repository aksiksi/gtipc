#include <stdio.h>
#include <pthread.h>

#include "gtipc_api.h"

/* Global state */
static int MODE = GTIPC_SYNC; // Mode defaults to SYNC

void *test_add(void *arg) {
    gtipc_arg_t *out = (gtipc_arg_t *)arg;
    out->res = out->x + out->y;
    return NULL;
}

int gtipc_init(gtipc_mode_t mode) {
    // Validate API mode
    switch (mode) {
        case GTIPC_SYNC:
        case GTIPC_ASYNC:
            MODE = mode;
            break;
        default:
            fprintf(stderr, "ERROR: Invalid API mode!\n");
            return GTIPC_MODE_ERROR;
    }

    // TODO
}

int gtipc_exit() {
    // Join all current IPC tasks
    if (MODE == GTIPC_ASYNC) {

    }

    // TODO
}

int gtipc_add(gtipc_arg_t *arg) {
    printf("Entering add!\n");

    printf("x = %d, y = %d\n", arg->x, arg->y);

    pthread_t test_thread;

    // Create a background thread
    if (pthread_create(&test_thread, NULL, test_add, (void *)arg)) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    // Wait for thread to join
    if (pthread_join(test_thread, NULL)) {
        fprintf(stderr, "Error joining thread\n");
        return 2;
    }

    return 0;
}
