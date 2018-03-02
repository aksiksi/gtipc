#ifndef GTIPC_API_H
#define GTIPC_API_H

#include "gtipc/types.h"

/* API error codes */
static int GTIPC_INIT_ERROR = 1;
static int GTIPC_FATAL_ERROR = 2;
static int GTIPC_RECV_ERROR = 3;
static int GTIPC_SEND_ERROR = 4;
static int GTIPC_SHM_ERROR = 5;

/* API initialization and exit */
extern int gtipc_init();
extern int gtipc_exit();

/* Synchronous API */

/**
 * Synchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @return 0 if no error
 */
extern int gtipc_sync(gtipc_arg *arg, gtipc_service service);

/* Asynchronous API */

/**
 * Asynchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @param id Return: unique identifier for current request.
 * @return 0 if no error
 */
extern int gtipc_async(gtipc_arg *arg, gtipc_service service, gtipc_request_id *id);

/**
 * Wait for an asynchronous request to complete.
 *
 * @param id Request ID
 * @param arg Result of service
 * @return 0 if no error
 */
extern int gtipc_async_get(gtipc_request_id id, gtipc_arg *arg);

#endif
