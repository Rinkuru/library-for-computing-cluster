#ifndef WORKER_H
#define WORKER_H

#include "common.h"
#include "master.h"

void hello_worker(void);

typedef struct {
    const char *host;
    int port;
    int max_time;
} WorkerConfig;

typedef struct {
    int threads;
    int cores;
} WorkerResources;

typedef struct {
    int socketFd;
    IntegralTask task;
    IntegralResult result;
    WorkerResources resources;
    int maxTime;
} Worker;

int WorkerInit(Worker *worker, const WorkerConfig *config, const WorkerResources *resources);

int WorkerRun(Worker *worker, Method method, Func f);

int WorkerSendResult(Worker *worker);

void WorkerDestroy(Worker *worker);

#endif
