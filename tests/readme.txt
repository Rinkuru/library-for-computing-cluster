Мне нужна именно библиотека.
Тоесть надо  чтоюы cluster собирался отдельно,
а всё что в папке tests был как отдельный проект который подключает эту бибилотеку.

Пример того как это сделать:
В tests/CMakeLists.txt написать вот это
cmake_minimum_required(VERSION 3.25)

project(cluster_tests C)

set(CLUSTER_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(CLUSTER_BUILD "${CMAKE_CURRENT_LIST_DIR}/../build")

add_library(cluster STATIC IMPORTED)

set_target_properties(cluster PROPERTIES
    IMPORTED_LOCATION "${CLUSTER_BUILD}/libcluster.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CLUSTER_ROOT}/include"
)

add_executable(integral_master integral_master.c)
target_link_libraries(integral_master PRIVATE cluster)

add_executable(integral_worker integral_worker.c)
target_link_libraries(integral_worker PRIVATE cluster)

Тогда его тоже собирать командами
cmake -S . -B build
cmake --build build

a запуск ./build/integral_master

А в корневом CMakeLists.txt закоментировать add_subdirectory(tests)
И убрать комментарий в run.sh