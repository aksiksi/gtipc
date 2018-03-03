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
    int shm_fd;
    size_t shm_size;
} client;

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

    // Create and open the shared memory object
    if ((client.shm_fd = shm_open(client.shm_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to create shared mem object\n");
        return GTIPC_SHM_ERROR;
    }

    // Set exact length required for shared mem object
    client.shm_size = sizeof(gtipc_shared_entry) * GTIPC_SHM_SIZE;
    ftruncate(client.shm_fd, client.shm_size);

    // Map shared memory with GTIPC_SHM_SIZE * gtipc_shared_entry
    client.shm_addr = mmap(NULL, client.shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, client.shm_fd, 0);
    if (client.shm_addr == MAP_FAILED) {
        fprintf(stderr, "FATAL: Failed to map shared memory region (errno %d)\n", errno);
        return GTIPC_SHM_ERROR;
    }

    // Initialize shared memory
    int i;
    gtipc_shared_entry se;
    se.used = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    for (i = 0; i < GTIPC_SHM_SIZE; i++) {
        // Copy over entire object to shared mem
        memcpy(client.shm_addr + i*sizeof(gtipc_shared_entry), &se, sizeof(gtipc_shared_entry));

        // Get handle to shared entry mutex
        gtipc_shared_entry *entry = (gtipc_shared_entry *)(client.shm_addr + i*sizeof(gtipc_shared_entry));
        pthread_mutex_t *mutex = &entry->mutex;

        // Init the mutex as shared
        pthread_mutex_init(mutex, &attr);
    }

    pthread_mutexattr_destroy(&attr);

    return 0;
}

/**
 * Destroy shared memory object used by client.
 * @return
 */
int destroy_shm_object() {
    // Close shm file descriptor
    close(client.shm_fd);

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

int resize_shm_object() {
    size_t new_shm_size = ((client.shm_size / sizeof(gtipc_shared_entry)) + GTIPC_SHM_SIZE) * sizeof(gtipc_shared_entry);

    // Reached max size, so do not resize
    if (new_shm_size > GTIPC_SHM_MAX_SIZE)
        return 0;

    // Open the shared mem object
    int fd;

    if ((fd = shm_open(client.shm_name, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to create shared mem object\n");
        return GTIPC_SHM_ERROR;
    }

    // Resize the object
    ftruncate(fd, new_shm_size);

    // Map the resized object and initialize it
    char *new_shm_addr = mmap(NULL, new_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (new_shm_addr == MAP_FAILED) {
        fprintf(stderr, "FATAL: Failed to remap shared memory region (errno %d)\n", errno);
        return GTIPC_SHM_ERROR;
    }

    // Initialize shared memory
    int i;
    gtipc_shared_entry se;
    se.used = 0;

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    for (i = 0; i < (new_shm_size / sizeof(gtipc_shared_entry)); i++) {
        // Copy over entire object to newly created shared mem
        memcpy(new_shm_addr + i*sizeof(gtipc_shared_entry), &se, sizeof(gtipc_shared_entry));

        // Get handle to shared entry mutex
        gtipc_shared_entry *entry = (gtipc_shared_entry *)(new_shm_addr + i*sizeof(gtipc_shared_entry));
        pthread_mutex_t *mutex = &entry->mutex;

        // Init the mutex as shared
        pthread_mutex_init(mutex, &attr);
    }

    pthread_mutexattr_destroy(&attr);

    // Notify the server about the resize operation
    gtipc_request req;
    req.request_id = -1;
    mq_send(client.send_queue, (char *)&req, sizeof(gtipc_request), 100); // High priority message

    // Wait for server to respond
    char buf[sizeof(gtipc_response)];
    gtipc_response *resp;
    mq_receive(client.recv_queue, (char *)buf, sizeof(gtipc_response), NULL);
    resp = (gtipc_response *)buf;

    // Server is ready to rumble, resize done!
    if (resp->request_id == -1) {
        // Unmap memory old shared mem
        munmap(client.shm_addr, client.shm_size);

        // Update global pointers
        client.shm_addr = new_shm_addr;
        client.shm_size = new_shm_size;
        client.shm_fd = fd;

        return 0;
    }

    return GTIPC_SHM_ERROR;
}

/**
 * Finds an unused shared memory entry, takes it, and returns its index.
 *
 * If first half of shared memory is full, client will expand the size of shared memory
 * in coordination with the server.
 *
 * @return index of entry
 */
int find_shm_entry(gtipc_arg *arg) {
    int idx = 0;
    char *target;
    gtipc_shared_entry *entry;

    size_t shm_size = client.shm_size / sizeof(gtipc_shared_entry);
    int half_full = (int)(shm_size / 2);

    // Iterate over all entries in shared memory
    while (1) {
        // The memory is half full, so resize it!
        if (idx == half_full)
            resize_shm_object();

        target = client.shm_addr + idx * sizeof(gtipc_shared_entry);
        entry = (gtipc_shared_entry *)target;
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

        idx = (idx + 1) % (int)shm_size;
    }

    return idx;
}

/**
 * Send IPC request to server and return ID.
 *
 * Request is sent over shared message queue.
 *
 * @return 0 if no error, or GTIPC_SEND_ERROR
 */
int send_request(gtipc_arg *arg, gtipc_service service, unsigned int priority, gtipc_request_key *key) {
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
    if (mq_send(client.send_queue, (char *)&req, sizeof(gtipc_request), priority)) {
        fprintf(stderr, "ERROR: Failed to send request to server\n");
        return GTIPC_SEND_ERROR;
    }

    // Key is the index of entry in shared memory
    *key = req.entry_idx;

    return 0;
}

/**
 * Check if a given request is completed in shared mem.
 */
int is_done(gtipc_request_key key, gtipc_arg *arg) {
    // Retrieve pointer to relevant entry
    char *target = client.shm_addr + key * sizeof(gtipc_shared_entry);
    gtipc_shared_entry *entry = (gtipc_shared_entry *)target;
    pthread_mutex_t *mutex = &entry->mutex;

    int done = 0;

    pthread_mutex_lock(mutex);

    if (entry->done == 1) {
        // Mark as unused
        entry->used = 0;
        *arg = entry->arg;
        done = 1;
    }

    pthread_mutex_unlock(mutex);

    return done;
}

/**
 * Wait for response from IPC server: continually spin on done flag in shared memory.
 *
 * @param entry_idx Index of shared memory entry to check
 * @param arg Result returned by server
 * @return
 */
int recv_response(gtipc_request_key key, gtipc_arg *arg) {
    // Retrieve pointer to relevant entry
    char *target = client.shm_addr + key * sizeof(gtipc_shared_entry);
    gtipc_shared_entry *entry = (gtipc_shared_entry *)target;

    // 0.1 ms backoff
    unsigned int backoff = 100;

    // Spin while not done
    while (1) {
        // Sleep for short period before acquiring the lock
        usleep(backoff);

        // Check if request served
        if (is_done(key, arg))
            break;

        // Exponential backoff
        backoff *= 2;
    }

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
    err = send_request(arg, service, 10, &request_id); if (err) return err;

    // Wait for correct response
    err = recv_response(request_id, arg); if (err) return err;

    return 0;
}

/**
 * Asynchronous API IPC service call.
 *
 * @param arg Argument to the service.
 * @param service Type of service required.
 * @param key Unique key for current request.
 * @return 0 if no error
 */
int gtipc_async(gtipc_arg *arg, gtipc_service service, gtipc_request_key *key) {
    // Send request to remote IPC server
    return send_request(arg, service, 1, key);
}

/**
 * Wait for a single asynchronous request to complete.
 *
 * @param key Request key
 * @param arg Result of service
 * @return 0 if no error
 */
int gtipc_async_wait(gtipc_request_key key, gtipc_arg *arg) {
    // Wait for correct response
    return recv_response(key, arg);
}

/**
 * Given a list of request keys, calls a function on each key and argument once the request is completed.
 *
 * @param keys An array of request keys
 * @param size Size (>= 0) of keys array
 * @param fn Function to call on each key and arg
 * @return
 */
int gtipc_async_map(gtipc_request_key *keys, int size, void (*fn)(gtipc_request_key, gtipc_arg *)) {
    if (size <= 0)
        return -1;

    int i = 0;
    int done = 0;

    // Allocate a key done array
    char *key_done = calloc((size_t)size, sizeof(char));

    gtipc_arg arg;

    while (1) {
        if (done == size)
            break;

        if (!key_done[i] && is_done(keys[i], &arg)) {
            done += 1;
            key_done[i] = 1;
            fn(keys[i], &arg);
        }

        i = (i + 1) % size;
    }

    return 0;
}

/**
 * Join on a group of async requests.
 *
 * @param keys Array of request keys
 * @param args Array of args into which results are written to
 * @param size Size of each array
 */
int gtipc_async_join(gtipc_request_key *keys, gtipc_arg *args, int size) {
    if (size <= 0)
        return -1;

    int i = 0;
    int done = 0;

    while (1) {
        if (done == size)
            break;

        done += is_done(keys[i], args + i);

        i = (i + 1) % size;
    }

    return 0;
}
