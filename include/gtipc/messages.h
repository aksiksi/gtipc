#ifndef GTIPC_MESSAGES_H
#define GTIPC_MESSAGES_H

#include <unistd.h>
#include <pthread.h>

#include "gtipc/types.h"

typedef struct __gtipc_registry {
    // Register or unregister current client
    gtipc_registry_cmd cmd;

    // Client's PID
    pid_t pid;

    // Send and receive queue names
    char send_queue_name[100];
    char recv_queue_name[100];

    // Shared memory name
    char shm_name[100];
} gtipc_registry;

/**
 * IPC request from client to server to initiate a service.
 */
typedef struct __gtipc_request {
    gtipc_service service;    // Requested IPC service
    gtipc_arg arg;            // Service argument
    int request_id;           // ID for current request
    int entry_idx;            // Index in shared mem for request's gtipc_shared_entry
    int pid;                  // Client PID
} gtipc_request;

/**
 * Entry in the shared memory segment accessible by both client and server.
 * Used to pass requests and arguments back and forth.
 */
typedef struct __gtipc_shared_entry {
    int used;
    int done;
    gtipc_arg arg;
    pthread_mutex_t mutex;
} gtipc_shared_entry;

/**
 * IPC response from server to client. Sent once service completed.
 */
typedef struct __gtipc_response {
    int request_id;
    gtipc_arg arg;
    int entry_idx;  // Index in shared mem for request's gtipc_shared_entry
} gtipc_response;

#endif
