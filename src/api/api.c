#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>

#include "gtipc/api.h"
#include "gtipc/messages.h"
#include "gtipc/params.h"

/* Global state */
// Global registry queue
mqd_t global_registry;
gtipc_registry registry_entry;

// Current request ID
volatile int global_request_id = 0;
pthread_spinlock_t global_request_lock;

// Mutex to sync multiple threads during request key search
pthread_mutex_t global_request_key_mutex;

// Whether or not library initialized
volatile int INIT = 0;

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
    pthread_mutex_t shm_resize_lock;
} client;

/**
 * Request queue for aysnc API requests.
 *
 * Functions as a priority queue, where head has highest prio and tail has lowest.
 */
typedef struct __request_queue {
    gtipc_request data;
    struct __request_queue *next;
} request_queue;

struct __request_queue_params {
    pthread_t thread;
    int stop_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    request_queue *queue;
} rqp;

void rq_enqueue(gtipc_request *req) {
    request_queue *entry = malloc(sizeof(request_queue));
    entry->data = *req;
    entry->next = NULL;

    pthread_mutex_lock(&rqp.mutex);

    request_queue *prev = NULL;
    request_queue *curr = rqp.queue;

    // Find first node whose successor has a *lower* priority
    while (curr != NULL && curr->data.prio >= req->prio) {
        prev = curr;
        curr = curr->next;
    }

    if (prev == NULL) {
        // Insert at head of queue
        entry->next = curr;
        rqp.queue = entry;
    } else {
        // Insert somewhere after head
        entry->next = curr;
        prev->next = entry;
    }

    pthread_mutex_unlock(&rqp.mutex);
}

int rq_deque(gtipc_request *req) {
    pthread_mutex_lock(&rqp.mutex);

    // Get head of queue (highest priority)
    request_queue *entry = rqp.queue;

    if (entry == NULL) {
        // Queue is empty
        pthread_mutex_unlock(&rqp.mutex);
        return -1;
    }

    *req = entry->data;

    // Point queue to next entry
    rqp.queue = entry->next;

    pthread_mutex_unlock(&rqp.mutex);

    free(entry);

    return 0;
}

void *rq_worker(void *unused) {
    gtipc_request req;
    struct mq_attr attr;

    while (!rqp.stop_thread) {
        // Get highest prio async request
        if (!rq_deque(&req)) {
            // Got one: send this request to server with given priority
            if (mq_send(client.send_queue, (char *)&req, sizeof(gtipc_request), req.prio)) {
                fprintf(stderr, "ERROR: Failed to send async request to server (errno: %d)\n", errno);
            }
        }
    }

    return NULL;
}

void async_rq_init() {
    rqp.mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    rqp.cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    rqp.queue = NULL;

    // Create request queue worker thread
    pthread_create(&rqp.thread, NULL, rq_worker, NULL);
    rqp.stop_thread = 0;
}

void async_rq_destroy() {
    pthread_mutex_lock(&rqp.mutex);

    // Cleanup the request queue
    request_queue *head = rqp.queue;
    request_queue *next = NULL;

    while (head != NULL) {
        next = head->next;
        free(head);
        head = next;
    }

    pthread_mutex_unlock(&rqp.mutex);

    // Cancel request queue worker thread
    rqp.stop_thread = 1;
    pthread_join(rqp.thread, NULL);

    // Destroy mutex and cond
    pthread_mutex_destroy(&rqp.mutex);
    pthread_cond_destroy(&rqp.cond);
}

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
        return GTIPC_CREATEQ_ERROR;
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
    se.done = 0;

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

    // Init resize lock
    client.shm_resize_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

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

    // Destory resize lock
    pthread_mutex_destroy(&client.shm_resize_lock);

    return 0;
}

int gtipc_init() {
    int err;

    if (INIT)
        return GTIPC_FATAL_ERROR;

    // Obtain write-only reference to global registry message queue
    global_registry = mq_open(GTIPC_REGISTRY_QUEUE, O_RDWR);

    if (global_registry == (mqd_t)-1) {
        fprintf(stderr, "FATAL: mq_open(global_registry) failed in gtipc_init()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Create client queues and check for errors
    err = create_queues(); if (err) return err;

    // Create shm object and resize lock
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

    global_request_key_mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;

    // Init async request queue
    async_rq_init();

    INIT = 1;

    return 0;
}

int gtipc_exit() {
    if (!INIT)
        return GTIPC_FATAL_ERROR;

    registry_entry.cmd = GTIPC_CLIENT_UNREGISTER;
    if (mq_send(global_registry, (const char *)&registry_entry, sizeof(gtipc_registry), 1)) {
        fprintf(stderr, "FATAL: Unregister message send failure in gtipc_exit()\n");
        return GTIPC_FATAL_ERROR;
    }

    // Destroy async request queue
    async_rq_destroy();

    // Cleanup queues
    destroy_queues();

    // Clean up shared mem
    destroy_shm_object();

    // Free up global spinlock
    pthread_spin_destroy(&global_request_lock);

    pthread_mutex_destroy(&global_request_key_mutex);

    INIT = 0;

    return 0;
}

int resize_shm_object() {
    size_t new_shm_size = ((client.shm_size / sizeof(gtipc_shared_entry)) + GTIPC_SHM_SIZE) * sizeof(gtipc_shared_entry);

    // Stop sending requests to the server!
    rqp.stop_thread = 1;
    pthread_join(rqp.thread, NULL);

    #if DEBUG
    printf("Resizing shared memory!\n");
    #endif

    // Reached max size, so do not resize
    if (new_shm_size > GTIPC_SHM_MAX_SIZE)
        return 0;

    // Acquire the resize lock
    pthread_mutex_lock(&client.shm_resize_lock);

    // Open the shared mem object
    int fd;

    if ((fd = shm_open(client.shm_name, O_RDWR, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "ERROR: Failed to create shared mem object\n");
        return GTIPC_SHM_ERROR;
    }

    // Resize the object
    ftruncate(fd, new_shm_size);

    // Map the resized object
    char *new_shm_addr = mmap(NULL, new_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (new_shm_addr == MAP_FAILED) {
        fprintf(stderr, "FATAL: Failed to remap shared memory region (errno %d)\n", errno);
        return GTIPC_SHM_ERROR;
    }

    // Synchronize shared memory with backing file so server can read it as-is
//    msync(new_shm_addr + client.shm_size, client.shm_size, MS_SYNC);

    // Notify the server about the resize operation
    gtipc_request req;
    req.request_id = -1;
    mq_send(client.send_queue, (char *)&req, sizeof(gtipc_request), 100); // High priority message

    // Wait for server to respond
    char buf[sizeof(gtipc_response)];
    gtipc_response *resp;
    mq_receive(client.recv_queue, (char *)buf, sizeof(gtipc_response), NULL);
    resp = (gtipc_response *)buf;

    // Server is ready to rumble, resize and init done!
    if (resp->request_id == -1) {
        // Unmap memory old shared mem
        munmap(client.shm_addr, client.shm_size);

        // Update global pointers
        client.shm_addr = new_shm_addr;
        client.shm_size = new_shm_size;
        client.shm_fd = fd;

        pthread_mutex_unlock(&client.shm_resize_lock);

        // Start request queue thread once more
        rqp.stop_thread = 0;
        pthread_create(&rqp.thread, NULL, rq_worker, NULL);

        #if DEBUG
        printf("Done resizing shared memory!\n");
        #endif

        return 0;
    }

    pthread_mutex_unlock(&client.shm_resize_lock);

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
            // Copy arg only if needed
            if (arg != NULL)
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
 * Create a GTIPC request and return it via passed in pointer.
 * @param arg
 * @param service
 * @param prio
 * @param key
 * @return
 */
int prepare_request(gtipc_arg *arg, gtipc_service service, gtipc_request_prio prio, gtipc_request *req) {
    if (prio < 0 || prio > 31)
        return GTIPC_PRIO_ERROR;

    req->service = service;
    req->pid = getpid();
    req->prio = prio;

    // Atomically set and increment global request ID
    pthread_spin_lock(&global_request_lock);
    req->request_id = global_request_id++;
    pthread_spin_unlock(&global_request_lock);

    // Find a suitable shared mem entry and store arg there
    // Synchronized to allow multiple client threads to rely on shared mem
    pthread_mutex_lock(&global_request_key_mutex);
    req->entry_idx = find_shm_entry(arg);
    pthread_mutex_unlock(&global_request_key_mutex);

    return 0;
}

/**
 * Check if a given request is completed in shared mem.
 */
int is_done(gtipc_request_key key, gtipc_arg *arg) {
    // Make sure no shared mem resize is taking place
    pthread_mutex_lock(&client.shm_resize_lock);

    // Retrieve pointer to relevant entry
    char *target = client.shm_addr + key * sizeof(gtipc_shared_entry);
    gtipc_shared_entry *entry = (gtipc_shared_entry *)target;
    pthread_mutex_t *mutex = &entry->mutex;

    pthread_mutex_unlock(&client.shm_resize_lock);

    int done = 0;

    pthread_mutex_lock(mutex);

    if (entry->done == 1) {
        // Mark as unused
        entry->used = 0;

        if (arg != NULL)
            *arg = entry->arg;

        // Not done anymore ;)
        entry->done = 0;

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
 * @param arg Input argument to the service; set to NULL if service takes no args
 * @param service Type of service required.
 * @param prio Priority
 * @param out Output argument returned by the service
 * @return 0 if no error
 */
int gtipc_sync(gtipc_arg *arg, gtipc_service service, gtipc_request_prio prio, gtipc_arg *out) {
    int err;

    // Create an IPC request object
    gtipc_request req;
    err = prepare_request(arg, service, prio, &req); if (err) return err;

    // Enqueue the request
    rq_enqueue(&req);

    // Wait for correct response
    int key = req.entry_idx;
    err = recv_response(key, out); if (err) return err;

    return 0;
}

/**
 * Asynchronous API IPC service call.
 *
 * @param arg Argument to the service; set to NULL if service takes no args
 * @param service Type of service required.
 * @param key Output: unique key for current request.
 * @return 0 if no error
 */
int gtipc_async(gtipc_arg *arg, gtipc_service service, gtipc_request_prio prio, gtipc_request_key *key) {
    // Create a request and store key
    gtipc_request req;
    int err = prepare_request(arg, service, prio, &req); if (err) return err;
    *key = req.entry_idx;

    // Enqueue request for later send to remote IPC server
    rq_enqueue(&req);

    return 0;
}

/**
 * Wait for a single asynchronous request to complete.
 *
 * @param key Request key
 * @param arg Result of service
 * @return 0 if no error
 */
int gtipc_async_wait(gtipc_request_key key, gtipc_arg *arg) {
    // Wait for correct response to complete
    while (!is_done(key, arg));
    return 0;
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

    free(key_done);

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

    // Track which tasks are done
    char *done_keys = malloc(sizeof(char) * size);
    memset(done_keys, 0, (size_t)size); // Zero out done_keys array

    while (1) {
        if (done == size)
            break;

        // Only check incomplete requests
        if (!done_keys[i]) {
            done_keys[i] = (char)is_done(keys[i], args + i);
            done += done_keys[i];
        }

        i = (i + 1) % size;
    }

    free(done_keys);

    return 0;
}
