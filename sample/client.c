#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "gtipc/api.h"

void *test_thread(void *unused) {
    gtipc_request_key keys[2];
    gtipc_arg args[3];

    // Perform random async task (low priority)
    gtipc_async(NULL, GTIPC_RAND, 1, &keys[0]); // No input argument required!

    // Next, perform a high-priority file service call
    gtipc_file_arg *file_arg = &args[1].file;
    static char* path = "/tmp/abc.txt";
    memcpy(file_arg->path, path, strlen(path));

    gtipc_async(&args[1], GTIPC_FILE, 25, &keys[1]); // Input is the file path

    // Now a high-priority rand synchronous call
    gtipc_sync(NULL, GTIPC_RAND, 20, &args[2]);

    gtipc_rand_arg *rand_sync = &args[2].rand;

    #if DEBUG
    printf("Sync rand from thread %d: %d, %d, %d, %d\n", getpid(), rand_sync->res[0], rand_sync->res[1],
           rand_sync->res[2], rand_sync->res[3]);
    #endif

    // Join both the async tasks
    gtipc_async_join(keys, args, 2);

    rand_sync = &args[0].rand;
    #if DEBUG
    printf("Async rand from thread %d: %d, %d, %d, %d\n", getpid(), rand_sync->res[0], rand_sync->res[1],
           rand_sync->res[2], rand_sync->res[3]);
    #endif

    // Read the file created by the file service
    FILE *fp = fopen(file_arg->path, "r");
    char line[100];
    fscanf(fp, "%s\n", line);
    printf("Read from %s: %s\n", file_arg->path, line);
    fclose(fp);

    return NULL;
}

int main() {
    int err, i, j;

    gtipc_init();

    int num_async = 10;
    int num_sync = 2;

    // Perform half of the async requests
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

    printf("Time to perform %d async requests: %ld usecs\n", num_async / 2, diff_usec);

    // Now perform all of the sync requests to rand service
    gtipc_arg sync_args[num_sync];
    priority = 10;

    for (i = 0; i < num_sync; i++) {
        err = gtipc_sync(NULL, GTIPC_RAND, priority, &sync_args[i]); // RAND takes no input

        #if DEBUG
        printf("Sync %d: rand bytes = %d, %d, %d, %d\n", i,
               sync_args[i].rand.res[0], sync_args[i].rand.res[1], sync_args[i].rand.res[2], sync_args[i].rand.res[3]);
        #endif

        if (i == num_sync / 2) {
            // Let's spawn three background threads, for fun!
            pthread_t t;

            for (j = 0; j < 3; j++) {
                pthread_create(&t, NULL, test_thread, NULL);
                pthread_detach(t);
            }
        }
    }

    // Now send out the remaining async tasks (higher priority)
    for (i = num_async / 2; i < num_async; i++) {
        async_args[i].mul.x = i * 20;
        async_args[i].mul.y = i * 22;
        err = gtipc_async(&async_args[i], GTIPC_MUL, priority, &keys[i]);
    }

    // Wait for ALL async requests to complete
    gtipc_async_join(keys, async_args, num_async);

    clock_gettime(CLOCK_REALTIME, &ts2);
    diff.tv_sec = ts2.tv_sec - ts1.tv_sec;
    diff.tv_nsec = ts2.tv_nsec - ts1.tv_nsec;
    diff_usec = diff.tv_sec * 1000000 + (long)(diff.tv_nsec / 1000.0);

    printf("Time to complete %d sync and %d async requests (in parallel): %ld usecs\n",
           num_sync, num_async, diff_usec);

    gtipc_exit();

    return 0;
}
