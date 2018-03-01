#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>

#include "gtipc/messages.h"
#include "gtipc/params.h"

#include "server.h"

/* Global state */
// Global registry queue
mqd_t global_registry;

// Registry thread
pthread_t registry_thread;

// Doubly linked list of registered clients
client_list *clients = NULL;

void exit_error(char *msg) {
    fprintf(stderr, "%s", msg);
    exit(EXIT_FAILURE);
}

void add(gtipc_arg *arg) {
    arg->res = arg->x + arg->y;
}

void mul(gtipc_arg *arg) {
    arg->res = arg->x * arg->y;
}

void compute_service(gtipc_request *req, int pid) {
    gtipc_arg arg = req->arg;

    // Print out info
    printf("Received from %d: service=%d, x=%d, y=%d\n", pid, req->service, arg.x, arg.y);

    sleep(1);

    // Perform computation
    switch (req->service) {
        case GTIPC_ADD:
            add(&arg);
            break;
        case GTIPC_MUL:
            mul(&arg);
            break;
        default:
            fprintf(stderr, "ERROR: Invalid service requested by client %d\n", pid);
    }
}

/**
 * Client thread handler. Each client gets its own thread.
 */
static void *client_handler(void *client_ptr) {
    // Get current thread's client object, as passed in
    client *client = &((client_list *)client_ptr)->client;

    // Buffer for incoming gtipc_request
    char buf[sizeof(gtipc_request)];
    gtipc_request *req;

    while (1) {
        // Wait for messages on receive queue
        mq_receive(client->recv_queue, buf, sizeof(gtipc_request), NULL);

        // Extract request and argument
        req = (gtipc_request *)buf;

        // Compute
        compute_service(req, client->pid);

        // Send back response
        gtipc_response resp;
        resp.request_id = req->request_id;
        resp.arg = req->arg;

        if (mq_send(client->send_queue, (char *)&resp, sizeof(gtipc_response), 1)) {
            fprintf(stderr, "ERROR: Failed to send response %d to client %d\n", resp.request_id, client->pid);
        }

        printf("Sent response to %d: x=%d, y=%d, res=%d\n", client->pid, resp.arg.x, resp.arg.y, resp.arg.res);
    }
}

/* Registry thread handler. */
static void *registry_handler(void *unused) {
    char recv_buf[sizeof(gtipc_registry)];
    gtipc_registry *reg;

    while (1) {
        // Wait for a new registry message from client (blocking)
        mq_receive(global_registry, recv_buf, sizeof(gtipc_registry), NULL);

        // Read out registry entry
        reg = (gtipc_registry *)recv_buf;

        #if DEBUG
        printf("CMD: %d, PID: %ld, Send queue: %s, Recv queue: %s\n", reg->cmd, (long)reg->pid,
               reg->send_queue_name, reg->recv_queue_name);
        #endif

        // Register or unregister the client based on given command
        switch (reg->cmd) {
            case GTIPC_CLIENT_REGISTER:
                register_client(reg);
                break;
            case GTIPC_CLIENT_UNREGISTER:
            case GTIPC_CLIENT_CLOSE:
                unregister_client(reg->pid, 0);
                break;
            default:
                fprintf(stderr, "ERROR: Incorrect registry command received from client %d\n", reg->pid);
        }
    }
}

/* Given a PID, return client object */
client_list *find_client(int pid) {
    client_list *list = clients;

    while (list != NULL && list->client.pid != pid) {
        list = list->next;
    }

    return list;
}

/* Remove a client from the given clients list */
void remove_client(client_list *node) {
    if (node != NULL) {
        node->prev->next = node->next;

        if (node->next != NULL)
            node->next->prev = node->prev;

        // If removing tail node, update global tail
        if (clients->tail == node)
            clients->tail = node->prev;
    }
}

/* Insert a client at TAIL of global clients list */
void append_client(client_list *node) {
    if (clients == NULL) {
        // Initialize the list
        node->tail = node;
        clients = node;
    }
    else {
        // Get tail of list
        client_list *tail = clients->tail;

        // Insert new node after tail
        node->prev = tail;
        node->next = NULL;
        node->tail = node;

        // Set old tail's next to new node
        tail->next = node;

        // Update global tail
        clients->tail = node;
    }
}

/**
 * Given an incoming registry object, create a client
 * and add it to the clients list.
 */
int register_client(gtipc_registry *reg) {
    // Create new client object based on given registry
    client client;

    client.pid = reg->pid;
    client.send_queue = mq_open(reg->recv_queue_name, O_RDWR);
    client.recv_queue = mq_open(reg->send_queue_name, O_RDWR);

    // Check for message queue errors
    if (client.send_queue == (mqd_t)-1 || client.recv_queue == (mqd_t)-1) {
        fprintf(stderr, "ERROR (%d): Client %d send and/or receive queue(s) failed to open\n", errno, client.pid);
        exit(EXIT_FAILURE);
    }

    // Append newly created client to global list
    client_list *node = malloc(sizeof(client_list));
    node->client = client;
    append_client(node);

    // Spin up client's background thread
    pthread_create(&client.client_thread, NULL, client_handler, (void *)node);

    #if DEBUG
        printf("Client %d has queues %d and %d\n", clients->client.pid, clients->client.send_queue, clients->client.recv_queue);
    #endif

    return 0;
}

int unregister_client(int pid, int close) {
    client_list *node = find_client(pid);

    if (node == NULL)
        return 0;

    // Get ref to current client object
    client *client = &node->client;

    // Send poison pill to client iff server is closing (i.e. close == 1)
    if (close) {
        // Create poison pill
        gtipc_registry registry;
        registry.cmd = GTIPC_SERVER_CLOSE;

        // Send poison pill to client
        if (mq_send(client->send_queue, (char *)&registry, sizeof(gtipc_registry), 1)) {
            fprintf(stderr, "ERROR: Could not send poison pill message to client %d\n", client->pid);
        }
    }

    // Remove current client from list
    remove_client(node);

    // Free up resources used by client
    mq_close(client->send_queue);
    mq_close(client->recv_queue);

    pthread_cancel(client->client_thread);

    free(node);

    return 0;
}

void exit_server() {
    // Close and unlink registry queue
    if (global_registry) {
        mq_close(global_registry);
        mq_unlink(GTIPC_REGISTRY_QUEUE);
    }

    // Perform client cleanup
    client_list *list = clients;

    while (list != NULL) {
        unregister_client(list->client.pid, 1);
        list = list->next;
    }
}

void signal_exit_server(int signo) {
    exit_server();
}

void init_server() {
    // Set exit handler for cleanup
    atexit(exit_server);

    // Setup signal handler for SIGINT cleanup
    signal(SIGINT, signal_exit_server);

    // Set queue attrs first to fix message size
    struct mq_attr attr;
    attr.mq_msgsize = sizeof(gtipc_registry);
    attr.mq_maxmsg = 10; // NOTE: must be <=10 for *unprivileged* process

    // Create the global registry queue
    global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

    if (global_registry == (mqd_t)-1) {
        // Already exists => unlink the queue first, then re-create
        if (errno == EEXIST) {
            mq_unlink(GTIPC_REGISTRY_QUEUE);
            global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
        }

        else
            exit_error("FATAL: Unable to create global registry\n");
    }

    // Spawn background thread for registry queue handling
    if (pthread_create(&registry_thread, NULL, registry_handler, NULL)) {
        exit_error("FATAL: Failed to create registry background thread!\n");
    }
}

int main(int argc, char **argv) {
    init_server();

    char in[2];

    // Wait for user exit
    while (in[0] != 'x') fgets(in, 2, stdin);

    exit_server();

    return 0;
}
