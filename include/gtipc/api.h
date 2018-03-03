#ifndef GTIPC_API_H
#define GTIPC_API_H

#include "gtipc/types.h"

/* API error codes */
static int GTIPC_INIT_ERROR = 1;
static int GTIPC_FATAL_ERROR = 2;
static int GTIPC_RECVQ_ERROR = 3;
static int GTIPC_SENDQ_ERROR = 4;
static int GTIPC_SHM_ERROR = 5;
static int GTIPC_PRIO_ERROR = 6;
static int GTIPC_CREATEQ_ERROR = 7;

/* API initialization and exit */
extern int gtipc_init();
extern int gtipc_exit();

/* Synchronous API */

/**
 * Synchronous API IPC service call.
 *
 * @param arg Input argument to the service; set to NULL if service takes no args
 * @param service Type of service required.
 * @param prio Priority of this request; between 0 (lowest) and 31 (highest)
 * @param out Output argument returned by the service
 * @return 0 if no error
 */
extern int gtipc_sync(gtipc_arg *arg, gtipc_service service, gtipc_request_prio prio, gtipc_arg *out);

/* Asynchronous API */

/**
 * Asynchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @param prio Priority of this request; 0 (lowest) and 31 (highest)
 * @param id Return: unique identifier for current request.
 * @return 0 if no error
 */
extern int gtipc_async(gtipc_arg *arg, gtipc_service service, gtipc_request_prio prio, gtipc_request_key *key);

/**
 * Wait for a single asynchronous request to complete.
 *
 * @param id Request ID
 * @param arg Result of service
 * @return 0 if no error
 */
extern int gtipc_async_wait(gtipc_request_key key, gtipc_arg *arg);

/**
 * Given a list of request keys, calls a callback function on each key and argument once request is completed.
 * @param keys An array of request of keys
 * @param size Size of keys array
 * @param fn Function to call on each key and arg
 * @return
 */
extern int gtipc_async_map(gtipc_request_key *keys, int size, void (*fn)(gtipc_request_key, gtipc_arg *));

/**
 * Join on a group of async requests.
 *
 * @param keys Array of request keys
 * @param args Array of args into which results are written to
 * @param size Size of each array
 */
extern int gtipc_async_join(gtipc_request_key *keys, gtipc_arg *args, int size);

#endif
