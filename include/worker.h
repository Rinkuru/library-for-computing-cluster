#ifndef WORKER_H
#define WORKER_H

#include "common.h"

void hello_worker(void);

typedef struct {
    const char *host;
    int port;
    int cores;
    int maxTime;
} WorkerConfig;

int WorkerRun(const WorkerConfig *config, Func f);

#endif
