#ifndef GTIPC_API_H
#define GTIPC_API_H

/* POSIX IPC parameters */
static char* GLOBAL_REGISTRY = "/gtipc/registry";

/* API error codes */
static int GTIPC_MODE_ERROR = 1; // Invalid mode provided at init time

/**
 * API argument structure
 *
 * x, y: inputs
 * res: output returned by API
 */
typedef struct gtipc_arg {
    int x, y, res;
} gtipc_arg_t;

/* Available API modes */
typedef enum gtipc_mode {
    GTIPC_SYNC = 0,
    GTIPC_ASYNC
} gtipc_mode_t;

/* API initialization and exit */
extern int gtipc_init(gtipc_mode_t mode);
extern int gtipc_exit();

/* Synchronous API */
extern int gtipc_add(gtipc_arg_t *arg);
extern int gtipc_mul(gtipc_arg_t *arg);

/* Asynchronous API */
extern void gtipc_add_async(gtipc_arg_t *arg);
extern void gtipc_mul_async(gtipc_arg_t *arg);

#endif
