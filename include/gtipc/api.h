#ifndef GTIPC_API_H
#define GTIPC_API_H

#include "gtipc/types.h"

/* API error codes */
static int GTIPC_INIT_ERROR = 1;
static int GTIPC_FATAL_ERROR = 2;
static int GTIPC_RECV_ERROR = 3;
static int GTIPC_SEND_ERROR = 4;

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
