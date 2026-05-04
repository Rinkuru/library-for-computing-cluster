cmake_minimum_required(VERSION 3.25)

project(computing_cluster C)

add_library(cluster STATIC
    src/master.c
    src/worker.c)

target_include_directories(cluster PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(tests)
