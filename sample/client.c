#include <stdio.h>

#include "gtipc/api.h"

int main() {
    gtipc_arg arg;
    arg.x = 10;
    arg.y = 11;

    // Init GTIPC API in synchronous mode
    gtipc_init(GTIPC_SYNC);

    int err = gtipc_add(&arg);

    printf("x = %d, y = %d, res = %d\n", arg.x, arg.y, arg.res);

    gtipc_exit();

    return 0;
}
