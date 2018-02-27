#include <stdio.h>

#include "gtipc_api.h"

int main() {
    printf("Hello, world!\n");

    gtipc_arg_t arg;
    arg.x = 10;
    arg.y = 11;

    int err = gtipc_add(&arg);

    printf("x = %d, y = %d, res = %d\n", arg.x, arg.y, arg.res);

    return 0;
}
