#include <stdio.h>
#include <unistd.h>
#include <gtipc/messages.h>
#include <sys/time.h>

#include "gtipc/api.h"

void map_func(gtipc_request_key key, gtipc_arg *arg) {
    printf("Request #%d completed: %d * %d = %d\n", key, arg->x, arg->y, arg->res);
}

int main() {
    gtipc_arg arg;

    int err, i;

    gtipc_init();

    int num_async = 1024;
    int num_sync = 10;

    // Perform 1024 async requests
    gtipc_arg async_args[num_async];
    gtipc_request_key keys[num_async];

    struct timespec ts1, ts2, diff;
    long diff_usec;
    clock_gettime(CLOCK_REALTIME, &ts1);

    for (i = 0; i < num_async; i++) {
        async_args[i].x = i * 20;
        async_args[i].y = i * 22;
        err = gtipc_async(&async_args[i], GTIPC_MUL, &keys[i]);
    }

    clock_gettime(CLOCK_REALTIME, &ts2);
    diff.tv_sec = ts2.tv_sec - ts1.tv_sec;
    diff.tv_nsec = ts2.tv_nsec - ts1.tv_nsec;
    diff_usec = diff.tv_sec * 1000000 + (long)(diff.tv_nsec / 1000.0);

    printf("Time to perform %d async requests: %ld usecs\n", num_async, diff_usec);

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

    clock_gettime(CLOCK_REALTIME, &ts2);
    diff.tv_sec = ts2.tv_sec - ts1.tv_sec;
    diff.tv_nsec = ts2.tv_nsec - ts1.tv_nsec;
    diff_usec = diff.tv_sec * 1000000 + (long)(diff.tv_nsec / 1000.0);

    printf("Time to complete %d sync and %d async requests (in parallel): %ld usecs\n",
           num_sync, num_async, diff_usec);

    gtipc_exit();

    return 0;
}
