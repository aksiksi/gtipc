#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gtipc/api.h"
#include "gtipc/messages.h"
#include "gtipc/params.h"

/* Global state */
// Flags and modes
static int MODE = GTIPC_SYNC; // Mode defaults to SYNC

// Global registry
mqd_t global_registry_queue;
gtipc_registry registry_entry;

// POSIX message queues and names
struct __gtipc_mq {
    mqd_t send_queue;
    mqd_t recv_queue;
    char send_queue_name[100];
    char recv_queue_name[100];
} gtipc_mq;

/* Create all required message queues */
int create_queues() {
    // Derive names for send and recv queues based on current PID
    pid_t pid = getpid();
    char *queue_names[] = {gtipc_mq.send_queue_name, gtipc_mq.recv_queue_name};
    char *queue_prefixes[] = {GTIPC_SENDQ_PREFIX, GTIPC_RECVQ_PREFIX};
    int i, written;

    // Perform string concat via sprintf (prefix + PID)
    for (i = 0; i < 2; i++) {
        written = 0;
        written += sprintf(queue_names[i], "%s", queue_prefixes[i]);
        sprintf(queue_names[i] + written, "%ld", (long)pid); // NOTE: sprintf sets null terminator
    }

    // Set custom attributes (message size and queue capacity)
    struct mq_attr attr;
    attr.mq_maxmsg = 10; // NOTE: must be <=10 for *unprivileged* process (see: http://man7.org/linux/man-pages/man7/mq_overview.7.html)
    attr.mq_msgsize = (1 << 13); // 8K byte messages (8K is the DEFAULT)

    // Finally, create the send and receive queues for current client
    // Queues are exclusively created by current process
    gtipc_mq.send_queue = mq_open(gtipc_mq.send_queue_name, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR, &attr);
    gtipc_mq.recv_queue = mq_open(gtipc_mq.recv_queue_name, O_EXCL | O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR, &attr);

    if (gtipc_mq.send_queue == (mqd_t)-1 || gtipc_mq.recv_queue == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open() failed in create_queues()\n");
        return GTIPC_FATAL_ERROR;
    }

    return 0;
}

void destroy_queues() {
    int i;
    mqd_t queues[] = {gtipc_mq.send_queue, gtipc_mq.recv_queue};
    char *queue_names[] = {gtipc_mq.send_queue_name, gtipc_mq.recv_queue_name};

    for (i = 0; i < 2; i++) {
        // Close the queue
        mq_close(queues[i]);

        // Unlink the queue to help kernel with ref. counting
        mq_unlink(queue_names[i]);
    }
}

void *test_add(void *arg) {
    gtipc_arg *out = (gtipc_arg *)arg;
    out->res = out->x + out->y;
    return NULL;
}

int gtipc_init(gtipc_mode mode) {
    // Validate API mode
    switch (mode) {
        case GTIPC_SYNC:
        case GTIPC_ASYNC:
            MODE = mode;
            break;
        default:
            fprintf(stderr, "ERROR: Invalid API mode!\n");
            return GTIPC_INIT_ERROR;
    }

    // Obtain write-only reference to global registry message queue
    global_registry_queue = mq_open(GTIPC_REGISTRY_QUEUE, O_WRONLY);

    if (global_registry_queue == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open(global_registry_queue) failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Create client queues and check for errors
    int err = create_queues(); if (err) return err;

    // Prepare registry entry for send
    registry_entry.pid = getpid();
    registry_entry.cmd = GTIPC_CLIENT_REGISTER;

    // Pass along queue names for easy lookups
    // Copy over names to registry entry
    strncpy(registry_entry.send_queue_name, gtipc_mq.send_queue_name, 100);
    strncpy(registry_entry.recv_queue_name, gtipc_mq.recv_queue_name, 100);

    #if DEBUG
    printf("send = %s, recv = %s\n", gtipc_mq.send_queue_name, gtipc_mq.recv_queue_name);
    #endif

    // Send a register message to register this client with server
    if (mq_send(gtipc_mq.send_queue, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: mq_send() failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    return 0;
}

int gtipc_exit() {
    // Join all current IPC tasks if in async mode
    if (MODE == GTIPC_ASYNC) {

    }

    // Send an unregister message to the server
    registry_entry.cmd = GTIPC_CLIENT_UNREGISTER;
    if (mq_send(gtipc_mq.send_queue, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: Unregister message send failure in gtipc_exit()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Cleanup queues
    destroy_queues();

    return 0;
}

int gtipc_add(gtipc_arg *arg) {
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
