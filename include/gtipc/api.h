#ifndef GTIPC_API_H
#define GTIPC_API_H

#include "gtipc/types.h"
#include "gtipc/messages.h"

/* API error codes */
static int GTIPC_MODE_ERROR = 1; // Invalid mode provided at init time

/* API initialization and exit */
extern int gtipc_init(gtipc_mode mode);
extern int gtipc_exit();

/* Synchronous API */
extern int gtipc_add(gtipc_arg *arg);
extern int gtipc_mul(gtipc_arg *arg);

/* Asynchronous API */
extern void gtipc_add_async(gtipc_arg *arg);
extern void gtipc_mul_async(gtipc_arg *arg);

#endif
