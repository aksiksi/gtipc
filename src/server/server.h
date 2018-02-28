#ifndef GTIPC_SERVER_H
#define GTIPC_SERVER_H

#include <unistd.h>

/**
 * Describes a single client as seen by the server.
 *
 * Note: **inverted** use of send and recv i.e. send is from server to client
 */
typedef struct __gtipc_client {
    pid_t pid;
    mqd_t send_queue;
    mqd_t recv_queue;
} gtipc_client;

/**
 * Doubly-linked list of clients.
 */
typedef struct __gtipc_client_node {
    gtipc_client client;
    struct __gtipc_client_node *next;
    struct __gtipc_client_node *prev;
} gtipc_client_list;

#endif
