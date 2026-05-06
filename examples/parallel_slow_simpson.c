#define _GNU_SOURCE

#include "integral.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_THREADS 4U
#define NUM_HARDWARE_THREADS 4U

#define INTEGRAL_BEGIN 0.0
#define INTEGRAL_END 1.0
#define INTEGRAL_EPS 0.000000000001
#define HARD_FUNC_FREQ 2000000.0

typedef struct {
    size_t threadI;
    Method method;
    Func f;
    double begin;
    double end;
    double result;
} ThreadArgs;

static double HardFunc(double x)
{
    return 2.0 + sin(HARD_FUNC_FREQ * x);
}

static void *ThreadFunc(void *threadArgs)
{
    ThreadArgs *args = (ThreadArgs *)threadArgs;
    if (args == NULL || args->method == NULL || args->f == NULL) {
        return NULL;
    }

    args->result = args->method(
        args->f,
        args->begin,
        args->end,
        INTEGRAL_EPS / NUM_THREADS);

    return NULL;
}

int main(void)
{
    const double length = INTEGRAL_END - INTEGRAL_BEGIN;
    ThreadArgs args[NUM_THREADS];
    pthread_t tids[NUM_THREADS];

    for (size_t i = 0U; i < NUM_THREADS; ++i) {
        const double partBegin = INTEGRAL_BEGIN + length * (double)i / NUM_THREADS;
        const double partEnd = INTEGRAL_BEGIN + length * (double)(i + 1U) / NUM_THREADS;

        args[i].threadI = i;
        args[i].begin = partBegin;
        args[i].end = partEnd;
        args[i].result = 0.0;
        args[i].method = SlowSimpson;
        args[i].f = HardFunc;

        pthread_attr_t threadAttributes;
        int ret = pthread_attr_init(&threadAttributes);
        if (ret != 0) {
            fprintf(stderr, "Unable to call pthread_attr_init: %s\n", strerror(ret));
            return EXIT_FAILURE;
        }

        cpu_set_t assignedHarts;
        CPU_ZERO(&assignedHarts);
        CPU_SET(i % NUM_HARDWARE_THREADS, &assignedHarts);

        ret = pthread_attr_setaffinity_np(
            &threadAttributes,
            sizeof(cpu_set_t),
            &assignedHarts);
        if (ret != 0) {
            fprintf(stderr, "Unable to call pthread_attr_setaffinity_np: %s\n", strerror(ret));
            pthread_attr_destroy(&threadAttributes);
            return EXIT_FAILURE;
        }

        ret = pthread_create(&tids[i], &threadAttributes, ThreadFunc, &args[i]);
        pthread_attr_destroy(&threadAttributes);
        if (ret != 0) {
            fprintf(stderr, "Unable to create thread: %s\n", strerror(ret));
            return EXIT_FAILURE;
        }
    }

    double result = 0.0;
    for (size_t i = 0U; i < NUM_THREADS; ++i) {
        int ret = pthread_join(tids[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "Unable to join thread: %s\n", strerror(ret));
            return EXIT_FAILURE;
        }

        result += args[i].result;
    }

    const double exact = 2.0 + (1.0 - cos(HARD_FUNC_FREQ)) / HARD_FUNC_FREQ;

    printf("parallel slow = %.16f\n", result);
    printf("exact         = %.16f\n", exact);
    printf("              = %.16f\n", INTEGRAL_EPS);

    return EXIT_SUCCESS;
}
