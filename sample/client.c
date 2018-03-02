#include <stdio.h>
#include <unistd.h>
#include <gtipc/messages.h>

#include "gtipc/api.h"

int main() {
    gtipc_arg arg;
    arg.x = 10;
    arg.y = 11;

    int err;

    gtipc_init();

    // Perform async request
    gtipc_request_id id;
    err = gtipc_async(&arg, GTIPC_MUL, &id);

    // Now perform sync request
    err = gtipc_sync(&arg, GTIPC_ADD);

    if (err) {
        printf("Error: %d\n", err);
        return 0;
    }

    printf("x = %d, y = %d, res = %d\n", arg.x, arg.y, arg.res);

    arg.x = 1000;

    sleep(2);

    gtipc_async_get(id, &arg);

    printf("x = %d, y = %d, res = %d\n", arg.x, arg.y, arg.res);

    gtipc_exit();

    return 0;
}
