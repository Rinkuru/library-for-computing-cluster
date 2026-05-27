#ifndef WORKER_H
#define WORKER_H

#include "common.h"

#include <stdbool.h>

void hello_worker(void);

typedef struct {
    const char *host;
    int port;
    int max_time;
    Method method;
} WorkerConfig;

typedef struct {
    int threads;
    int cores;
    int firstCore;
} WorkerResources;

typedef struct {
    int socketFd;
    ClusterPacket task;
    ClusterPacket result;
    WorkerResources resources;
    int maxTime;
} Worker;

typedef struct {
    Method method;
    ClusterThreadTask task;
    ClusterPacket *result;
    bool finished;
    bool joined;
} WorkerThreadArgs;

int WorkerInit(Worker *worker, const WorkerConfig *config, const WorkerResources *resources);

int WorkerRun(Worker *worker, Method method);

int WorkerSendResult(Worker *worker);

void WorkerDestroy(Worker *worker);

#endif
