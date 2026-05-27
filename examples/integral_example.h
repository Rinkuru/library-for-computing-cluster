#ifndef INTEGRAL_EXAMPLE_H
#define INTEGRAL_EXAMPLE_H

enum {
    IntegralDefaultWorkerThreads = 2
};

typedef struct {
    double begin;
    double end;
    double eps;
} IntegralTask;

typedef struct {
    double value;
} IntegralResult;

#endif
