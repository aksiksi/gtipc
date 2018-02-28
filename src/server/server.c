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

gtipc_client_list *clients; // Doubly linked list of clients

/* Remove a client from the given clients list */
void remove_client(gtipc_client_list *node) {
    if (node != NULL) {
        node->prev->next = node->next;

        if (node->next != NULL)
            node->next->prev = node->prev;

        free(node);
    }
}

/* Insert a client at TAIL of global clients list */
void append_client(gtipc_client *client) {
    gtipc_client_list *node = malloc(sizeof(gtipc_client_list));

    node->client = *client;
    node->prev = clients;
    node->next = NULL;

    clients = node;
}

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
    // Close and unlink registry queue
    mq_close(global_registry_queue);
    mq_unlink(GTIPC_REGISTRY_QUEUE);

    // Perform client cleanup
    gtipc_registry registry;
    gtipc_client_list *list = clients;
    gtipc_client client;

    while (list != NULL) {
        // Send poison pill to client
        registry.cmd = GTIPC_SERVER_CLOSE;
        if (mq_send(client.send_queue, (char *)&registry, sizeof(gtipc_registry), 1)) {
            fprintf(stderr, "ERROR: Could not send poison pill message to client %d\n", client.pid);
        }

        // Remove current client from list and free up resources
        remove_client(list);

        list = list->next;
    }
}

int main(int argc, char **argv) {
    init_server();

    char recv_buf[sizeof(gtipc_registry)];
    gtipc_registry *reg;

    // Print out first incoming message from client
    mq_receive(global_registry_queue, recv_buf, sizeof(gtipc_registry), NULL);
    reg = (gtipc_registry *)recv_buf;
    printf("CMD: %d, PID: %ld, Send queue: %s, Recv queue: %s\n", reg->cmd, (long)reg->pid,
           reg->send_queue_name, reg->recv_queue_name);

    // Add the client to global clients list
    // Note how queues are flipped from this end
    gtipc_client client;
    client.pid = reg->pid;
    client.send_queue = mq_open(reg->recv_queue_name, O_RDWR);
    client.recv_queue = mq_open(reg->send_queue_name, O_RDWR);

    append_client(&client);

    printf("Client %d has queues %d and %d\n", clients->client.pid, clients->client.send_queue, clients->client.recv_queue);

    exit_server();

    return 0;
}
