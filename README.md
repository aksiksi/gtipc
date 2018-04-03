# GTIPC

This project was implemented for CS6210 (Advanced Operating Systems) at Georgia Tech in Spring 2018.

## Overview

The goal GTIPC is to implement a low-level IPC client/server protocol and interface targeting the Unix platform.

The main requirements for this project are:

1. Implement a blocking IPC client that can communicate with some server process.
2. Implement a non-blocking/asynchronous IPC client that performs the same tasks.
3. Design a client API and library that can be used in a generic C/C++ Unix application.
4. Present a sample application that utilizes the aforementioned client API.

The GTIPC API interface provides clients with three "dummy" IPC services:

1. A service that can multiply two 32-bit integers (MUL service).
2. A service that generates four pseudorandom 32-bit integers (RAND service).
3. A service that creates and/or appends a line of text to a given file path (FILE service).

See `gtipc/types.h` for the structure of arguments to each of these services.

## Build

* To build everything: `make`
* To build just the GTIPC static library: `make lib`
* To build only the sample client application: `make client`

All build artifacts will be in `bin/`.

## Sample Client

A sample client is included in `sample/client.c`.

### Build and Run

To build, simply run `make` in the root directory.

* To run the server: `./bin/gtipc-server` (**do this first**)
* To run the sample client `./bin/gtipc-client`

### Operation

1. Initialize the GTIPC library
2. 1024 asynchronous requests to the MUL service (triggers shared memory resize)
3. 128 synchronous (blocking) requests to the RAND service
4. Spawns 10 background threads that each perform (in parallel):
    a. An asynchronous request to RAND
    b. An asynchronous request to the FILE service
    c. A synchronous request to RAND
    d. Join both of the asynchronous requests
5. 1024 asynchronous requests to the MUL service (back on main thread)
6. Join all 2048 asynchronous requests
7. Exit the GTIPC library

### Performance Results

Environment: Ubuntu (v4.10 kernel), GCC 5.4.0, 2 CPUs

* Time to dispatch first 1024 async requests: **21299 usecs**
* Time to complete 128 sync and 2048 async requests: **898769 usecs**

## Implementing Your Own Client

1. In your client application: `#include "gtipc/api.h"`.
2. Point your compiler to the `includes/` directory when compiling, and then point to `libgtipc.a` during linking.
3. Make sure to start the GTIPC server before testing your application!
