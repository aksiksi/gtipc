#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "gtipc/api.h"
#include "gtipc/messages.h"
#include "gtipc/params.h"

/* Global state */
// Global registry
mqd_t global_registry;
gtipc_registry registry_entry;

// Current request ID
volatile int global_request_id = 0;
pthread_spinlock_t global_request_lock;

// Message queues and shared memory for current client
struct __client {
    // Queues
    mqd_t send_queue;
    mqd_t recv_queue;
    char send_queue_name[100];
    char recv_queue_name[100];

    // Shared memory
    char *shm_addr;
    char shm_name[100];
    pthread_mutex_t *shm_mutex;
    size_t shm_size;
} client;

// Callback registry


/* Create all required message queues */
int create_queues() {
    // Derive names for send and recv queues based on current PID
    pid_t pid = getpid();
    char *queue_names[] = {client.send_queue_name, client.recv_queue_name};
    char *queue_prefixes[] = {GTIPC_SENDQ_PREFIX, GTIPC_RECVQ_PREFIX};
    int i, written;

    // Perform string concat via sprintf (prefix + PID)
    for (i = 0; i < 2; i++) {
        written = 0;
        written += sprintf(queue_names[i], "%s", queue_prefixes[i]);
        sprintf(queue_names[i] + written, "%ld", (long)pid); // NOTE: sprintf sets null terminator
    }

    // Set custom attributes (message size and queue capacity)
    struct mq_attr attr;
    attr.mq_maxmsg = 10; // NOTE: must be <=10 for *unprivileged* process (see: http://man7.org/linux/man-pages/man7/mq_overview.7.html)

    // Finally, create the send and receive queues for current client
    // Queues are exclusively created by current process
    attr.mq_msgsize = sizeof(gtipc_request);
    client.send_queue = mq_open(client.send_queue_name, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR, &attr);

    attr.mq_msgsize = sizeof(gtipc_response);
    client.recv_queue = mq_open(client.recv_queue_name, O_EXCL | O_CREAT | O_RDONLY, S_IRUSR | S_IWUSR, &attr);

    if (client.send_queue == (mqd_t)-1 || client.recv_queue == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open() failed in create_queues()\n");
        return GTIPC_FATAL_ERROR;
    }

    return 0;
}

void destroy_queues() {
    int i;

    mqd_t queues[] = {client.send_queue, client.recv_queue};
    char *queue_names[] = {client.send_queue_name, client.recv_queue_name};

    for (i = 0; i < 2; i++) {
        // Close the queue
        mq_close(queues[i]);

        // Unlink the queue to help kernel with ref. counting
        mq_unlink(queue_names[i]);
    }
}

/**
 * Create a new shared memory for current client.
 * @return 0 if no error
 */
int create_shm_object() {
    // Perform string concat via sprintf (prefix + PID)
    int written = 0;
    written += sprintf(client.shm_name, "%s", GTIPC_SHM_PREFIX);
    sprintf(client.shm_name + written, "%ld", (long)getpid());

    int fd;

    // Create and open the shared memory object
    if ((fd = shm_open(client.shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to create shared mem object\n");
        return GTIPC_SHM_ERROR;
    }

    // Set exact length required for shared mem object
    // First entry in shared mem is a mutex, followed by all shared entries
    client.shm_size = sizeof(gtipc_shared_entry) * GTIPC_SHM_SIZE;
    ftruncate(fd, client.shm_size);

    // Map shared memory with GTIPC_SHM_SIZE * gtipc_shared_entry
    client.shm_addr = mmap(NULL, client.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (client.shm_addr == MAP_FAILED) {
        fprintf(stderr, "FATAL: Failed to map shared memory region (errno %d)\n", errno);
        return GTIPC_SHM_ERROR;
    }

    // Initialize shared memory
    int i;
    gtipc_shared_entry se;
    se.used = 0;

    for (i = 0; i < GTIPC_SHM_SIZE; i++) {
        // Copy over entire object to shared mem
        memcpy(client.shm_addr + i*sizeof(gtipc_shared_entry), &se, sizeof(gtipc_shared_entry));

        // Get handle to shared entry mutex
        gtipc_shared_entry *entry = (gtipc_shared_entry *)(client.shm_addr + i*sizeof(gtipc_shared_entry));
        pthread_mutex_t *mutex = &entry->mutex;

        // Init the mutex as shared
        pthread_mutexattr_t attr;
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    // Descriptor not needed anymore
    close(fd);

    return 0;
}

/**
 * Destroy shared memory object used by client.
 * @return
 */
int destroy_shm_object() {
    // Unlink the shared mem object
    if (shm_unlink(client.shm_name)) {
        fprintf(stderr, "ERROR: Failed to unlink shared mem object\n");
        return GTIPC_SHM_ERROR;
    }

    // Unmap memory
    munmap(client.shm_addr, client.shm_size);

    return 0;
}

int gtipc_init() {
    int err;

    // Obtain write-only reference to global registry message queue
    global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_RDWR);

    if (global_registry == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open(global_registry) failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Create client queues and check for errors
    err = create_queues(); if (err) return err;

    // Create shm object
    err = create_shm_object(); if (err) return err;

    // Prepare registry entry for send
    registry_entry.pid = getpid();
    registry_entry.cmd = GTIPC_CLIENT_REGISTER;

    // Pass along queue and shared mem names for server-side access
    // Copy over names to registry entry
    strncpy(registry_entry.send_queue_name, client.send_queue_name, 100);
    strncpy(registry_entry.recv_queue_name, client.recv_queue_name, 100);
    strncpy(registry_entry.shm_name, client.shm_name, 100);

    #if DEBUG
    printf("send = %s, recv = %s, shm = %s\n", client.send_queue_name, client.recv_queue_name, client.shm_name);
    #endif

    // Send a register message to register this client with server
    if (mq_send(global_registry, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: mq_send(global_registry) failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Reset request ID and init spinlock
    global_request_id = 0;
    pthread_spin_init(&global_request_lock, PTHREAD_PROCESS_PRIVATE);

    return 0;
}

int gtipc_exit() {
    // Send an unregister message to the server
    registry_entry.cmd = GTIPC_CLIENT_UNREGISTER;
    if (mq_send(global_registry, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: Unregister message send failure in gtipc_exit()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Cleanup queues
    destroy_queues();

    // Clean up shared mem
    destroy_shm_object();

    // Free up global spinlock
    pthread_spin_destroy(&global_request_lock);

    return 0;
}

/**
 * Finds an unused shared memory entry, takes it, and returns its index.
 * Note that this function will *block* until an unused entry is found.
 *
 * @return index of entry
 */
int find_shm_entry(gtipc_arg *arg) {
    int idx = 0;
    gtipc_shared_entry *entry;

    // Iterate over all entries in shared memory
    while (1) {
        entry = (gtipc_shared_entry *)(client.shm_addr + idx * sizeof(gtipc_shared_entry));
        pthread_mutex_t *mutex = &entry->mutex;

        pthread_mutex_lock(mutex);

        if (!entry->used) {
            // Unused entry found
            entry->arg = *arg;
            entry->used = 1;
            entry->done = 0;
            pthread_mutex_unlock(mutex);
            break;
        }

        pthread_mutex_unlock(mutex);

        idx = (idx + 1) % GTIPC_SHM_SIZE;
    }

    return idx;
}

/**
 * Send IPC request to server and return ID.
 *
 * @return 0 if no error, or GTIPC_SEND_ERROR
 */
int send_request(gtipc_arg *arg, gtipc_service service, gtipc_request_id *request_id) {
    // Create an IPC request object
    gtipc_request req;
    req.service = service;
    req.arg = *arg;
    req.pid = getpid();

    // Atomically increment global request ID
    pthread_spin_lock(&global_request_lock);
    req.request_id = global_request_id++;
    pthread_spin_unlock(&global_request_lock);

    // Find a suitable shared mem entry and store arg there
    req.entry_idx = find_shm_entry(arg);

    // Send request to server (blocks if queue full)
    // TODO: fix this for the case of async request
    if (mq_send(client.send_queue, (char *)&req, sizeof(gtipc_request), 1)) {
        fprintf(stderr, "ERROR: Failed to send request to server\n");
        return GTIPC_SEND_ERROR;
    }

    // Request ID is the index of entry in shared memory
    *request_id = req.entry_idx;

    return 0;
}

/**
 * Wait for response from IPC server: continually spin on done flag in shared memory.
 *
 * @param entry_idx Index of shared memory entry to check
 * @param arg Result returned by server
 * @return
 */
int recv_response(int entry_idx, gtipc_arg *arg) {
    // Retrieve pointer to relevant entry
    gtipc_shared_entry *entry;
    entry = (gtipc_shared_entry *)(client.shm_addr + entry_idx * sizeof(gtipc_shared_entry));
    pthread_mutex_t *mutex = &entry->mutex;

    // Spin on done flag
    while (1) {
        pthread_mutex_lock(mutex);

        if (entry->done == 1) {
            pthread_mutex_unlock(mutex);
            break;
        }

        pthread_mutex_unlock(mutex);
    }

    // Obtain result returned by server
    *arg = entry->arg;

    // Set entry as unused
    pthread_mutex_lock(client.shm_mutex);
    entry->used = 0;
    pthread_mutex_unlock(client.shm_mutex);

    return 0;
}

/**
 * Synchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @return 0 if no error
 */
int gtipc_sync(gtipc_arg *arg, gtipc_service service) {
    int err;

    // Send request to remote IPC server
    int request_id;
    err = send_request(arg, service, &request_id); if (err) return err;

    // Wait for correct response
    err = recv_response(request_id, arg); if (err) return err;

    return 0;
}

/**
 * Asynchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @param id Unique identifier for current request.
 * @return 0 if no error
 */
int gtipc_async(gtipc_arg *arg, gtipc_service service, gtipc_request_id *id) {
    // Send request to remote IPC server
    return send_request(arg, service, id);
}

/**
 * Wait for an asynchronous request to complete.
 *
 * @param id Request ID
 * @param arg Result of service
 * @return 0 if no error
 */
int gtipc_async_get(gtipc_request_id id, gtipc_arg *arg) {
    // Wait for correct response
    return recv_response(id, arg);
}
