#ifndef GTIPC_SERVER_H
#define GTIPC_SERVER_H

#include <unistd.h>
#include <mqueue.h>
#include <pthread.h>

// Worker thread management for a single client
#define THREADS_PER_CLIENT 10

/**
 * Describes a single client as seen by the server.
 *
 * Note: **inverted** use of send and recv i.e. send is from server to client.
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

    // Client worker threads
    int stop_client_threads;
    int num_threads_started;
    int num_threads_completed;
    pthread_mutex_t threads_mutex;
    pthread_cond_t threads_cond;
    pthread_t workers[THREADS_PER_CLIENT];
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

/* Client list management */
client_list *find_client(int pid);
void remove_client(client_list *node);
void append_client(client_list *node);

/* POSIX IPC setup and cleanup */
void open_shm_object(gtipc_registry *reg, client *client);
void *resize_shm_object(void *c);

/* Internal functions */
void init_server();
void exit_server();

/* Server API functions */
void mul_service(gtipc_mul_arg *arg);
void rand_service(gtipc_rand_arg *arg);
int file_service(gtipc_file_arg *arg); // Return error if file error
void handle_request(gtipc_request *req, client *client);

#endif
