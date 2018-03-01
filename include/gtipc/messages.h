#ifndef GTIPC_MESSAGES_H
#define GTIPC_MESSAGES_H

#include <unistd.h>

#include "gtipc/types.h"

typedef struct __gtipc_registry {
    // Register or unregister current client
    gtipc_registry_cmd cmd;

    // Client's PID
    pid_t pid;

    // Send and receive queue names
    char send_queue_name[100];
    char recv_queue_name[100];
} gtipc_registry;

typedef struct __gtipc_request {
    gtipc_service service;
    gtipc_arg arg;
    int request_id;
} gtipc_request;

typedef struct __gtipc_response {
    int request_id;
    gtipc_arg arg;
} gtipc_response;

#endif
