#include <stdio.h>
#include <gtipc/messages.h>

#include "gtipc/api.h"

int main() {
    int err, i;

    gtipc_init();

    int num_async = 512;
    int num_sync = 75;

    // Perform async requests
    gtipc_arg async_args[num_async];
    gtipc_request_key keys[num_async];
    gtipc_request_prio priority = 1;

    struct timespec ts1, ts2, diff;
    long diff_usec;
    clock_gettime(CLOCK_REALTIME, &ts1);

    for (i = 0; i < (num_async / 2); i++) {
        async_args[i].mul.x = i * 20;
        async_args[i].mul.y = i * 22;
        err = gtipc_async(&async_args[i], GTIPC_MUL, priority, &keys[i]);
    }

    clock_gettime(CLOCK_REALTIME, &ts2);
    diff.tv_sec = ts2.tv_sec - ts1.tv_sec;
    diff.tv_nsec = ts2.tv_nsec - ts1.tv_nsec;
    diff_usec = diff.tv_sec * 1000000 + (long)(diff.tv_nsec / 1000.0);

    printf("Time to perform %d async requests: %ld usecs\n", num_async, diff_usec);

    // Now perform sync rand requests
    gtipc_arg sync_args[num_sync];
    priority = 10;

    for (i = 0; i < num_sync; i++) {
        err = gtipc_sync(&sync_args[i], GTIPC_RAND, priority);

        printf("Sync %d: rand bytes = %d, %d, %d, %d\n", i,
               sync_args[i].rand.res[0], sync_args[i].rand.res[1], sync_args[i].rand.res[2], sync_args[i].rand.res[3]);
    }

    // Now more async
    for (i = num_async / 2; i < num_async; i++) {
        async_args[i].mul.x = i * 20;
        async_args[i].mul.y = i * 22;
        err = gtipc_async(&async_args[i], GTIPC_MUL, priority, &keys[i]);
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
