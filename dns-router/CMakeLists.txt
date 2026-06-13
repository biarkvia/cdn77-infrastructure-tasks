cmake_minimum_required(VERSION 3.14)
project(CDNDNS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

add_executable(dns_router 
    src/main.cpp 
    src/Router.cpp
)