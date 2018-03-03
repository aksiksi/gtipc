#ifndef GTIPC_SERVER_H
#define GTIPC_SERVER_H

#include <unistd.h>
#include <mqueue.h>
#include <pthread.h>

/**
 * Queue for thread requests.
 */
typedef struct __request_queue {
    gtipc_request data;
    struct __request_queue *tail;
    struct __request_queue *next;
} request_queue;

// Worker thread management for a single client
#define NUM_THREADS 10
typedef struct __client_workers {
    request_queue *queue;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t threads[NUM_THREADS];
} client_workers;

/**
 * Describes a single client as seen by the server.
 *
 * Note: **inverted** use of send and recv i.e. send is from server to client
 */
typedef struct __client {
    // Client PID
    pid_t pid;

    // Queues
    mqd_t send_queue;
    mqd_t recv_queue;

    // Shared memory
    char *shm_addr;
    size_t shm_size;
    char shm_name[100];

    // Reference to client's thread handler
    pthread_t client_thread;
    int stop_client_thread;
    int num_threads_started;
    int num_threads_completed;
    pthread_mutex_t completed_mutex;
    client_workers workers;
} client;

/**
 * Doubly-linked list of clients.
 */
typedef struct __client_node {
    client client;
    struct __client_node *tail;
    struct __client_node *next;
    struct __client_node *prev;
} client_list;

/* Client register functions */
int register_client(gtipc_registry *reg);
int unregister_client(int pid, int close);

/* POSIX IPC setup and cleanup */
void open_shm_object(gtipc_registry *reg, client *client);
void resize_shm_object(client *client);

/* Internal functions */
void init_server();
void exit_server();

/* Server API functions */
void add(gtipc_arg *arg);
void mul(gtipc_arg *arg);
void compute_service(gtipc_request *req, client *client);

#endif
