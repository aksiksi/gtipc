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
mqd_t global_registry;
gtipc_registry registry_entry;

// Current request ID
volatile int global_request_id = 0;

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

    // Finally, create the send and receive queues for current client
    // Queues are exclusively created by current process
    attr.mq_msgsize = sizeof(gtipc_request);
    gtipc_mq.send_queue = mq_open(gtipc_mq.send_queue_name, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR, &attr);

    attr.mq_msgsize = sizeof(gtipc_response);
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
    global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_RDWR);

    if (global_registry == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open(global_registry) failed in gtipc_init()\n");
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
    if (mq_send(global_registry, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: mq_send(global_registry) failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Reset request ID
    global_request_id = 0;

    return 0;
}

int gtipc_exit() {
    // Join all current IPC tasks if in async mode
    if (MODE == GTIPC_ASYNC) {

    }

    // Send an unregister message to the server
    registry_entry.cmd = GTIPC_CLIENT_UNREGISTER;
    if (mq_send(global_registry, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: Unregister message send failure in gtipc_exit()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Cleanup queues
    destroy_queues();

    return 0;
}

/**
 * Send IPC request to server and return ID.
 * @return 0 if no error, or GTIPC_SEND_ERROR
 */
int send_request(gtipc_arg *arg, gtipc_service service, int *request_id) {
    // Create an IPC request object
    gtipc_request req;
    req.service = service;
    req.arg = *arg;
    req.request_id = global_request_id++;

    // Send request to server
    if (mq_send(gtipc_mq.send_queue, (char *)&req, sizeof(gtipc_request), 1)) {
        fprintf(stderr, "ERROR: Failed to send request to server\n");
        return GTIPC_SEND_ERROR;
    }

    *request_id = req.request_id;

    return 0;
}

/**
 * Wait for response from IPC server.
 * @param arg
 * @return
 */
int recv_response(int request_id, gtipc_arg *arg) {
    char buf[sizeof(gtipc_response)];
    gtipc_response *resp = NULL;

    // Wait for reply on recv queue
    // TODO: Figure out why receive is failing (?)
    while (!mq_receive(gtipc_mq.recv_queue, buf, sizeof(gtipc_response), NULL)) {
        resp = (gtipc_response *)buf;
        if (resp->request_id == request_id)
            break;
    }

    if (resp == NULL) {
        fprintf(stderr, "ERROR: No response received from server\n");
        return GTIPC_RECV_ERROR;
    } else if (resp->request_id != request_id) {
        fprintf(stderr, "ERROR: Incorrect response received (expecting: %d, received: %d)\n",
                request_id, resp->request_id);
        return GTIPC_RECV_ERROR;
    }

    *arg = resp->arg;

    return 0;
}


int gtipc_add(gtipc_arg *arg) {
    int err;

    #if DEBUG
    fprintf(stderr, "INFO: Entering add!\n");
    fprintf(stderr, "INFO: x = %d, y = %d\n", arg->x, arg->y);
    #endif

    // Send request to remote IPC server
    int request_id;
    err = send_request(arg, GTIPC_ADD, &request_id); if (err) return err;

    // Wait for correct response
    err = recv_response(request_id, arg); if (err) return err;

    return 0;
}

int gtipc_mul(gtipc_arg *arg) {
    int err;

    #if DEBUG
    fprintf(stderr, "INFO: Entering mul!\n");
    fprintf(stderr, "INFO: x = %d, y = %d\n", arg->x, arg->y);
    #endif

    // Send request to remote IPC server
    int request_id;
    err = send_request(arg, GTIPC_ADD, &request_id); if (err) return err;

    // Wait for correct response
    err = recv_response(request_id, arg); if (err) return err;

    return 0;
}
