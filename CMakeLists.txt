cmake_minimum_required(VERSION 3.5)
project(gtipc)

set(CMAKE_CXX_STANDARD 11)

include_directories(include/)

# gtipc API library
add_library(gtipc
        include/gtipc_api.h
        src/api/gtipc_api.c)

# Link in libruntime and pthread
target_link_libraries(gtipc rt pthread)

target_compile_definitions(gtipc PUBLIC -DDEBUG=1)

# gtipc client
add_executable(gtipc-client
        sample/client.c
        include/gtipc_api.h)

add_dependencies(gtipc-client gtipc)

target_link_libraries(gtipc-client gtipc)

# gtipc server
add_executable(gtipc-server
        src/server/gtipc_server.c
        src/server/gtipc_server.h)

# Link in libruntime and pthread
target_link_libraries(gtipc-server rt pthread)
