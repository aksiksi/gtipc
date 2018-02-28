#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <errno.h>

#include "gtipc/messages.h"
#include "gtipc/params.h"

#include "server.h"

void exit_error(char *msg) {
    fprintf(stderr, "%s", msg);
    exit(EXIT_FAILURE);
}

/* Global server state */
mqd_t global_registry_queue;

void *test_add(void *arg) {
    int *x = (int *)arg;
    (*x)++;
    return NULL;
}

void init_server() {
    // Set queue attrs first to fix message size
    struct mq_attr attr;
    attr.mq_msgsize = sizeof(gtipc_registry);
    attr.mq_maxmsg = 10; // NOTE: must be <=10 for *unprivileged* process

    // Create the global registry queue
    global_registry_queue = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

    if (global_registry_queue == (mqd_t)-1) {
        // Already exists => unlink the queue first, then re-create
        if (errno == EEXIST) {
            mq_unlink(GTIPC_REGISTRY_QUEUE);
            global_registry_queue = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
        }

        else
            exit_error("FATAL: Unable to create global registry\n");
    }
}

void exit_server() {
    // Clean up the message queue
    mq_close(global_registry_queue);
    mq_unlink(GTIPC_REGISTRY_QUEUE);
}

int main(int argc, char **argv) {
    init_server();

    char recv_buf[sizeof(gtipc_registry)];
    gtipc_registry *reg;

    // Print out first incoming message from client
    mq_receive(global_registry_queue, recv_buf, sizeof(gtipc_registry), NULL);
    reg = (gtipc_registry *)recv_buf;
    printf("CMD: %d, PID: %ld, Send queue: %s, Recv queue: %s\n", reg->reg, (long)reg->pid,
           reg->send_queue_name, reg->recv_queue_name);

    exit_server();

    return 0;
}
