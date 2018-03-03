#ifndef GTIPC_TYPES_H
#define GTIPC_TYPES_H

/**
 * MUL API argument structure
 *
 * x, y: inputs
 * res: product of two inputs
 */
typedef struct __gtipc_mul_arg {
    int x, y, res;
} gtipc_mul_arg;

/**
 * RAND API argument
 *
 * res[4]: 4 random words returned by the service
 */
typedef struct __gtipc_rand_arg {
    int res[4];
} gtipc_rand_arg;

/**
 * FILE API argument
 *
 * path[150]: path to file to be created by service
 */
typedef struct __gtipc_file_arg {
    char path[150];
} gtipc_file_arg;

/**
 * Generic argument to GTIPC API
 *
 * Only one of the members should be set!
 */
typedef union __gtipc_arg {
    gtipc_mul_arg mul;
    gtipc_rand_arg rand;
    gtipc_file_arg file;
} gtipc_arg;

/* Available IPC services */
typedef enum __gtipc_service {
    GTIPC_MUL,
    GTIPC_RAND,
    GTIPC_FILE
} gtipc_service;

/* IPC registry commands */
typedef enum __gtipc_registry_cmd {
    GTIPC_CLIENT_REGISTER,
    GTIPC_CLIENT_UNREGISTER,
    GTIPC_CLIENT_CLOSE,
    GTIPC_SERVER_CLOSE
} gtipc_registry_cmd;

// Unique per-request key used to retrieve result from shared memory
typedef int gtipc_request_key;

// Request priority
typedef unsigned int gtipc_request_prio;

#endif //GTIPC_TYPES_H
