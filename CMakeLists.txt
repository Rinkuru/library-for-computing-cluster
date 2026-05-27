cmake_minimum_required(VERSION 3.25)

project(computing_cluster C)

find_package(Threads REQUIRED)

add_library(cluster STATIC
    src/master.c
    src/worker.c
    src/common_multiplexing.c)

target_include_directories(cluster PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cluster PUBLIC m Threads::Threads)

add_subdirectory(examples)
