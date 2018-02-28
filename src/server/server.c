#include <stdio.h>
#include <pthread.h>

#include "server.h"

void *test_add(void *arg) {
    int *x = (int *)arg;
    (*x)++;
    return NULL;
}

int main(int argc, char **argv) {
    printf("Hello, world!\n");

    int x = 10;

    printf("x = %d\n", x);

    pthread_t test_thread;

    if (pthread_create(&test_thread, NULL, test_add, &x)) {
        fprintf(stderr, "Error creating thread\n");
        return 1;
    }

    printf("x = %d\n", x);

    return 0;
}
