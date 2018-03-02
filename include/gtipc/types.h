#ifndef GTIPC_TYPES_H
#define GTIPC_TYPES_H

/**
 * API argument structure
 *
 * x, y: inputs
 * res: output returned by API
 */
typedef struct __gtipc_arg {
    int x, y, res;
} gtipc_arg;

/* Available API modes */
typedef enum __gtipc_mode {
    GTIPC_SYNC = 0,
    GTIPC_ASYNC
} gtipc_mode;

/* Available IPC services */
typedef enum __gtipc_service {
    GTIPC_ADD,
    GTIPC_MUL
} gtipc_service;

/* IPC registry commands */
typedef enum __gtipc_registry_cmd {
    GTIPC_CLIENT_REGISTER,
    GTIPC_CLIENT_UNREGISTER,
    GTIPC_CLIENT_CLOSE,
    GTIPC_SERVER_CLOSE
} gtipc_registry_cmd;

typedef int gtipc_request_id;

#endif //GTIPC_TYPES_H
