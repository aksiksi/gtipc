#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include "gtipc/messages.h"
#include "gtipc/params.h"

#include "server.h"

/* Global state */
// Global registry queue
mqd_t global_registry;

// Registry thread
pthread_t registry_thread;
static int STOP_REGISTRY = 0; // Flag to stop registry thread

// Doubly linked list of registered clients
client_list *clients = NULL;

void exit_error(char *msg) {
    fprintf(stderr, "%s", msg);
    exit(EXIT_FAILURE);
}

/**
 * Multiply two numbers, copy result to shared memory, then busy loop
 * @param arg
 */
void mul_service(gtipc_mul_arg *arg) {
    // Long running multiply
    int i;
    for (i = 0; i < 100000; i++)
        arg->res = arg->x * arg->y;
}

/**
 * Generate four pseudo-random numbers and copy them to shared memory
 * @param arg
 */
void rand_service(gtipc_rand_arg *arg) {
    arg->res[0] = rand();
    arg->res[1] = rand();
    arg->res[2] = rand();
    arg->res[3] = rand();
}

/**
 * Create/append to a file at given path
 * @param arg
 */
int file_service(gtipc_file_arg *arg) {
    FILE *fp;

    if ((fp = fopen(arg->path, "a")) == NULL)
        return -1;

    fprintf(fp, "Hello from service %d!\n", getpid());

    fclose(fp);

    return 0;
}

/**
 * A single worker thread for a particular client.
 * Waits for request to be available on client queue.
 *
 * @param data Pointer to client
 * @return
 */
void *service_worker(void *data) {
    client *client = data;

    // Buffer for incoming gtipc_request
    char buf[sizeof(gtipc_request)];
    unsigned int prio;
    gtipc_request *req;

    // Timeout parameter for message queue operations
    struct timespec ts;

    while (!client->stop_client_threads) {
        // Setup a 1 ms timeout
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 1000000;
        ts.tv_sec = 0;

        // Wait for service request from client on receive queue
        ssize_t received = mq_timedreceive(client->recv_queue, buf, sizeof(gtipc_request), &prio, &ts);

        // If receive error, ignore the request
        if (received != -1) {
            // Extract request`
            req = (gtipc_request *)buf;

            if (req->request_id == -1) {
                // Resize request received from client
                resize_shm_object(client);
            }

            else {
                pthread_mutex_lock(&client->started_mutex);
                client->num_threads_started++;
                pthread_mutex_unlock(&client->started_mutex);

                // Handle received request
                handle_request(req, client);

                // Increment number of threads completed
                pthread_mutex_lock(&client->completed_mutex);
                client->num_threads_completed++;
                pthread_mutex_unlock(&client->completed_mutex);
            }
        }
    }
}

void handle_request(gtipc_request *req, client *client) {
    gtipc_shared_entry *entry = (gtipc_shared_entry *)(client->shm_addr + req->entry_idx * sizeof(gtipc_shared_entry));
    gtipc_arg *arg = &entry->arg;

    // Perform computation based on requested service
    switch (req->service) {
        case GTIPC_MUL:
            mul_service(&arg->mul);
            break;
        case GTIPC_RAND:
            rand_service(&arg->rand);
            break;
        case GTIPC_FILE:
            if (file_service(&arg->file))
                fprintf(stderr, "ERROR: Could not open (%s) for client %d!\n", arg->file.path, client->pid);
            break;
        default:
            fprintf(stderr, "ERROR: Invalid service requested by client %d\n", req->pid);
    }

    // Mark entry in shared memory as DONE (i.e., request has been served)
    pthread_mutex_t *mutex = &entry->mutex;
    pthread_mutex_lock(mutex);
    entry->done = 1;
    pthread_mutex_unlock(mutex);

    printf("Client %d, request %d, %d: DONE\n", client->pid, req->request_id, req->service);
}

void init_worker_threads(client *client) {
    client->stop_client_threads = 0;
    client->num_threads_started = 0;
    client->num_threads_completed = 0;
    client->completed_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    client->started_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    int i;

    // Spawn the worker threads; they will wait for requests on the client request message queue
    for (i = 0; i < THREADS_PER_CLIENT; i++)
        pthread_create(&client->workers[i], NULL, service_worker, (void *)client);
}

void cleanup_worker_threads(client *client) {
    pthread_mutex_destroy(&client->completed_mutex);
    pthread_mutex_destroy(&client->started_mutex);

    int i;

    // Cancel worker threads for current client
    for (i = 0; i < THREADS_PER_CLIENT; i++)
        pthread_cancel(client->workers[i]);
}

/* Registry thread handler. */
static void *registry_handler(void *unused) {
    char recv_buf[sizeof(gtipc_registry)];
    gtipc_registry *reg;


    // Timeout parameter for message queue operations
    struct timespec ts;

    while (!STOP_REGISTRY) {
        // 100 ms receive timeout
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 100000000;
        ts.tv_sec = 0;

        // Wait for a new registry message from client
        if (mq_timedreceive(global_registry, recv_buf, sizeof(gtipc_registry), NULL, &ts) == -1)
            continue;

        // Read out registry entry
        reg = (gtipc_registry *)recv_buf;

        printf("CMD: %d, PID: %ld, Send queue: %s, Recv queue: %s\n", reg->cmd, (long)reg->pid,
               reg->send_queue_name, reg->recv_queue_name);

        // Register or unregister the client based on given command
        switch (reg->cmd) {
            case GTIPC_CLIENT_REGISTER:
                register_client(reg);
                break;
            case GTIPC_CLIENT_UNREGISTER:
            case GTIPC_CLIENT_CLOSE:
                unregister_client(reg->pid, 0);
                break;
            default:
                fprintf(stderr, "ERROR: Incorrect registry command received from client %d\n", reg->pid);
        }
    }
}

/* Given a PID, return client object */
client_list *find_client(int pid) {
    client_list *list = clients;

    while (list != NULL && list->client.pid != pid) {
        list = list->next;
    }

    return list;
}

/* Remove a client from the given clients list */
void remove_client(client_list *node) {
    if (node != NULL) {
        // If current is head AND tail, empty the list
        if (node->prev == NULL && node->next == NULL)
            clients = NULL;
        else {
            // If current is not head node, update previous node
            if (node->prev != NULL)
                node->prev->next = node->next;

            // If current is not tail node, update next
            if (node->next != NULL)
                node->next->prev = node->prev;
            else {
                // Removing tail node, so update global tail
                clients->tail = node->prev;
            }
        }
    }
}

/* Insert a client at TAIL of global clients list */
void append_client(client_list *node) {
    if (clients == NULL) {
        // Initialize the list
        node->next = NULL;
        node->prev = NULL;
        node->tail = node;

        clients = node;
    }
    else {
        // Get tail of list
        client_list *tail = clients->tail;

        // Insert new node after tail
        node->prev = tail;
        node->next = NULL;
        node->tail = node;

        // Set old tail's next to new node
        tail->next = node;

        // Update global tail
        clients->tail = node;
    }
}

/**
 * Open a shared memory object based on given name.
 * @param reg
 * @param client
 */
void open_shm_object(gtipc_registry *reg, client *client) {
    // Map shared memory
    int fd;

    if ((fd = shm_open(reg->shm_name, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to open shared mem object for client %d\n", client->pid);
        exit(EXIT_FAILURE);
    }

    memcpy(client->shm_name, reg->shm_name, 100);

    // Determine size of shared mem object
    struct stat s;
    fstat(fd, &s);
    client->shm_size = (size_t)s.st_size;

    // Map shared memory based on size determined
    client->shm_addr = mmap(NULL, client->shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    close(fd);
}

/**
 * Resize the shared mem object in coordination with client.
 */
void resize_shm_object(client *client) {
    int fd;

    // Open new shared memory object
    if ((fd = shm_open(client->shm_name, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to resize shared mem object for client %d\n", client->pid);
        exit(EXIT_FAILURE);
    }

    // Determine size of new shared mem object
    struct stat s;
    fstat(fd, &s);
    size_t new_shm_size = (size_t)s.st_size;

    // Map new shared memory segment
    char *new_shm_addr = mmap(NULL, new_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // Wait for all started threads to complete
    while (client->num_threads_completed != client->num_threads_started);

    // Copy over old shared segment to new segment
    memcpy(new_shm_addr, client->shm_addr, client->shm_size);

    // Send notification to client that new segment is ready
    gtipc_response resp;
    resp.request_id = -1;
    mq_send(client->send_queue, (char *)&resp, sizeof(gtipc_response), 1);

    // Switch shared memory refs
    client->shm_addr = new_shm_addr;
    client->shm_size = new_shm_size;

    close(fd);
}

/**
 * Given an incoming registry object, create a client
 * and add it to the clients list.
 */
int register_client(gtipc_registry *reg) {
    // Create new client object based on given registry
    client client;

    client.pid = reg->pid;

    // Open queues
    client.send_queue = mq_open(reg->recv_queue_name, O_RDWR);
    client.recv_queue = mq_open(reg->send_queue_name, O_RDWR);

    // Check for message queue errors
    if (client.send_queue == (mqd_t)-1 || client.recv_queue == (mqd_t)-1) {
        fprintf(stderr, "ERROR (%d): Client %d send and/or receive queue(s) failed to open\n", errno, client.pid);
        exit(EXIT_FAILURE);
    }

    // Open the shared mem object
    open_shm_object(reg, &client);

    // Append newly created client to global list
    client_list *node = malloc(sizeof(client_list));
    node->client = client;
    append_client(node);

    // Init worker thread state
    init_worker_threads(&node->client);

    // Spin up

    #if DEBUG
        printf("Client %d has queues %d and %d\n", clients->client.pid, clients->client.send_queue, clients->client.recv_queue);
    #endif

    return 0;
}

int unregister_client(int pid, int close) {
    client_list *node = find_client(pid);

    if (node == NULL)
        return 0;

    // Get ref to current client object
    client *client = &node->client;

    // Send poison pill to client iff server is closing (i.e. close == 1)
    if (close) {
        // Create poison pill
        gtipc_registry registry;
        registry.cmd = GTIPC_SERVER_CLOSE;

        // Send poison pill to client
        // TODO: FIX THIS SO THAT SEND QUEUE RECEIVE REGISTRY MESSAGES
        if (mq_send(client->send_queue, (char *)&registry, sizeof(gtipc_registry), 1)) {
            fprintf(stderr, "ERROR: Could not send poison pill message to client %d\n", client->pid);
        }
    }

    // Remove current client from list
    remove_client(node);

    // Free up resources used by client
    // Close queues
    mq_close(client->send_queue);
    mq_close(client->recv_queue);

    // Wait for all threads to complete
    while (client->num_threads_started != client->num_threads_completed);

    // Cleanup client thread state
    cleanup_worker_threads(client);

    // Unmap shared memory
    munmap(client->shm_addr, client->shm_size);

    // Free memory consumed by client
    free(node);

    return 0;
}

void exit_server() {
    // Perform client cleanup
    client_list *list = clients;

    while (list != NULL) {
        unregister_client(list->client.pid, 1);
        list = list->next;
    }

    // Close and unlink registry queue
    if (global_registry) {
        mq_close(global_registry);
        mq_unlink(GTIPC_REGISTRY_QUEUE);
    }

    // Stop registry thread and join
    STOP_REGISTRY = 1;
    pthread_join(registry_thread, NULL);
}

void init_server() {
    // Set exit handler for cleanup
    atexit(exit_server);

    // Set queue attrs first to fix message size
    struct mq_attr attr;
    attr.mq_msgsize = sizeof(gtipc_registry);
    attr.mq_maxmsg = 10; // NOTE: must be <=10 for *unprivileged* process

    // Create the global registry queue
    global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);

    if (global_registry == (mqd_t)-1) {
        // Already exists => unlink the queue first, then re-create
        if (errno == EEXIST) {
            mq_unlink(GTIPC_REGISTRY_QUEUE);
            global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_EXCL | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attr);
        }

        else
            exit_error("FATAL: Unable to create global registry\n");
    }

    // Spawn background thread for registry queue handling
    if (pthread_create(&registry_thread, NULL, registry_handler, NULL)) {
        exit_error("FATAL: Failed to create registry background thread!\n");
    }

    // Time-based seed
    srand(time((NULL)));
}

int main(int argc, char **argv) {
    init_server();

    char in[2];

    // Wait for user exit
    while (in[0] != 'x') fgets(in, 2, stdin);

    exit_server();

    return 0;
}
