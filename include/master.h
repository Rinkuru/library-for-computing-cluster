#ifndef MASTER_H
#define MASTER_H

#include <stdlib.h>
#include <unistd.h>

void hello_master(void);

typedef struct {
    const char *host;
    int port;
    int required_workers;
    int max_time_ms;
} MasterConfig;

typedef struct {
    double begin;
    double end;
    double eps;
} IntegralTask;

typedef struct {
    double value;
} IntegralResult;

typedef struct {
    int socketFd;
} MasterWorker;

typedef struct {
    int listenSocketFd;
    MasterWorker *workers;
    int workersCount;
    int workersCapacity;
} Master;

int MasterInit(Master *master, const MasterConfig *config);

int MasterRun(
    const Master *master,
    const IntegralTask *tasks,
    IntegralResult *results
);

int MasterComputeIntegral(
    const MasterConfig *config,
    const IntegralTask *task,
    IntegralResult *result
);

typedef struct {
    long workerI;
    int socketFd;
    IntegralTask task;
    IntegralResult *result;
    int status;
} MasterThreadArgs;

static void MasterPrepareEmpty(Master *master) {
    master->listenSocketFd = -1;
    master->workers = NULL;
    master->workersCount = 0;
    master->workersCapacity = 0;
}

static void MasterDestroy(Master *master) {
    if (master == NULL) return;

    if (master->workers != NULL) {
        for (int i = 0; i < master->workersCapacity; ++i) {
            if (master->workers[i].socketFd >= 0) {
                close(master->workers[i].socketFd);
            }
        }

        free(master->workers);
    }

    if (master->listenSocketFd >= 0) {
        close(master->listenSocketFd);
    }

    MasterPrepareEmpty(master);
}

#endif
