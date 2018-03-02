#ifndef GTIPC_PARAMS_H
#define GTIPC_PARAMS_H

#include <string.h>

/* POSIX IPC parameters */

// Prefix for *ALL* POSIX IPC names
static char* GTIPC_PREFIX = "/gtipc-";

// Prefixes for POSIX message queues
static char* GTIPC_SENDQ_PREFIX = "/gtipc-queue-send-";
static char* GTIPC_RECVQ_PREFIX = "/gtipc-queue-recv-";

// Prefix for POSIX shared memory
static char* GTIPC_SHM_PREFIX = "/gtipc-shmxyz-";

// Number of gtipc_arg elements in shared segment
static int GTIPC_SHM_SIZE = 1024;

// Name of global client registry queue (created once by server)
static char* GTIPC_REGISTRY_QUEUE = "/gtipc-queue-registry";

#endif //GTIPC_PARAMS_H
