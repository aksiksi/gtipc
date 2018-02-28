#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include "gtipc/api.h"
#include "gtipc/params.h"

/* Global state */

// Flags and modes
static int MODE = GTIPC_SYNC; // Mode defaults to SYNC

// Global registry
mqd_t global_registry_queue;
gtipc_registry registry_entry;

// POSIX message queues and names
mqd_t send_queue;
mqd_t recv_queue;
char *send_queue_name;
char *recv_queue_name;

/* Create all required message queues */
void create_queues() {
    // Allocate 100 characters for each queue name
    // Name consists of prefix + PID
    send_queue_name = malloc(100);
    recv_queue_name = malloc(100);

    if (send_queue_name == NULL || recv_queue_name == NULL) {
        fprintf(stderr, "FATAL: malloc() failed in create_queues()!\n");
        exit(EXIT_FAILURE);
    }

    // Derive names for send and recv queues based on current PID
    pid_t pid = getpid();

    char *queue_names[] = {send_queue_name, recv_queue_name};
    char *queue_prefixes[] = {GTIPC_SENDQ_PREFIX, GTIPC_RECVQ_PREFIX};
    int i, written;

    for (i = 0; i < 2; i++) {
        written = 0;
        written += sprintf(queue_names[i], "%s", queue_prefixes[i]);
        sprintf(queue_names[i] + written, "%ld", (long)pid); // NOTE: sprintf sets null terminator
    }

    // Finally, create the send and receive queues for current client
    // Set custom attributes (message size and queue capacity)
    struct mq_attr attr;
    attr.mq_maxmsg = 100; // Default is 10 (see: http://man7.org/linux/man-pages/man7/mq_overview.7.html)
    attr.mq_msgsize = (1 << 14); // 16K byte messages

    send_queue = mq_open(send_queue_name, O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR, &attr);
    recv_queue = mq_open(recv_queue_name, O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR, &attr);

    if (send_queue || recv_queue) {
        fprintf(stderr, "FATAL: mq_open() failed in create_queues()!\n");
        exit(EXIT_FAILURE);
    }
}

void destroy_queues() {
    // Close the queues
    mq_close(send_queue);
    mq_close(recv_queue);

    // Unlink the queues to help kernel with ref counting
    mq_unlink(send_queue_name);
    mq_unlink(recv_queue_name);

    // Free up queue names
    free(send_queue_name);
    free(recv_queue_name);
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
            return GTIPC_MODE_ERROR;
    }

    // Obtain write-only reference to global registry message queue
    global_registry_queue = mq_open(GTIPC_REGISTRY_QUEUE, O_WRONLY, S_IRUSR | S_IWUSR);

    if (global_registry_queue) {
        fprintf(stderr, "FATAL: mq_open(global_registry_queue) failed in gtipc_init()!\n");
        exit(EXIT_FAILURE);
    }

    // Create client queues
    create_queues();

    // Send a register message to register this client with the server
    // Pass along queue names for easy lookups
    registry_entry.pid = getpid();
    registry_entry.reg = GTIPC_CLIENT_REGISTER;
    registry_entry.send_queue_name = send_queue_name;
    registry_entry.recv_queue_name = recv_queue_name;

    if (mq_send(send_queue, (char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: mq_send() failed in gtipc_init()!\n");
        exit(EXIT_FAILURE);
    }
}

int gtipc_exit() {
    // Join all current IPC tasks if in async mode
    if (MODE == GTIPC_ASYNC) {

    }

    // Send an unregister message to the server
    registry_entry.reg = GTIPC_CLIENT_UNREGISTER;
    mq_send(send_queue, (char *)&registry_entry, sizeof(gtipc_registry), 1);

    // Cleanup queues
    destroy_queues();
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
