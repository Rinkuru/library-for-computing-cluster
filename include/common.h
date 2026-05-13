#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <time.h>

typedef double (*Func)(double x);

typedef double (*Method)(Func f, double a, double b, double eps);

static long NowMs(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "[NowMs] Unable NowMs\n");
        return -1;
    }

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

#endif
