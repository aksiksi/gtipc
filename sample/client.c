#include <stdio.h>
#include <unistd.h>
#include <gtipc/messages.h>

#include "gtipc/api.h"

void map_func(gtipc_request_key key, gtipc_arg *arg) {
    printf("Request #%d completed: %d * %d = %d\n", key, arg->x, arg->y, arg->res);
}

int main() {
    gtipc_arg arg;

    int err, i;

    gtipc_init();

    int num_async = 16;
    int num_sync = 10;

    // Perform 1024 async requests
    gtipc_arg async_args[num_async];
    gtipc_request_key keys[num_async];

    for (i = 0; i < num_async; i++) {
        async_args[i].x = i * 20;
        async_args[i].y = i * 22;
        err = gtipc_async(&async_args[i], GTIPC_MUL, &keys[i]);
    }

    // Now perform 10 sync requests
    gtipc_arg sync_args[num_sync];

    for (i = 0; i < num_sync; i++) {
        sync_args[i].x = i * 10;
        sync_args[i].y = i * 12;

        err = gtipc_sync(&sync_args[i], GTIPC_ADD);

        printf("Sync %d: %d + %d = %d\n", i,
               sync_args[i].x, sync_args[i].y, sync_args[i].res);
    }

    // Now get all async requests
    gtipc_async_join(keys, async_args, num_async);

//    for (i = 0; i < 2048; i++) {
//        gtipc_async_wait(keys[i], &async_args[i]);
//        printf("Async %d: %d * %d = %d\n", i,
//               async_args[i].x, async_args[i].y, async_args[i].res);
//    }

    gtipc_exit();

    return 0;
}
