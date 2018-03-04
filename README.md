# CS6210: Project 2

For this project, we were tasked with building a Unix-based IPC client/server interface for some service.

The client library needed to provide both sync and async versions of service calls.

## Build

* To build everything: `make`
* To build only the sample client application: `make client`
* To build just the library: `make lib`

All artifacts will be in `bin/`.

## Run

* To run the server: `./bin/gtipc-server`
* To run the test client `./bin/gtipc-client`

Note: you **must** run the server binary first!

## Usage in Your Application

In your application: `#include "gtipc/api.h"`.

Point your compiler to the `includes/` directory when compiling, and then point to `libgtipc.a` during linking.
