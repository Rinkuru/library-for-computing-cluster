#ifndef MASTER_H
#define MASTER_H

#include "common.h"

#include <stdlib.h>
#include <unistd.h>

void hello_master(void);

typedef struct {
    const char *host;
    int port;
    int required_workers;
    int max_time_ms;
} MasterConfig;

typedef enum {
    MasterWorkerEmpty = 0,
    SendTaskSize,
    SendTaskData,
    ReadResultSize,
    ReadResultData,
    Done,
    Failed
} MasterWorkerState;

typedef struct {
    int socketFd;
    MasterWorkerState state;
    size_t transferOffset;
    size_t taskOffset;
    size_t taskSize;
    size_t resultSize;
    unsigned char sizeBuffer[ClusterSizeHeaderSize];
} MasterWorker;

typedef struct {
    int listenSocketFd;
    MasterWorker *workers;
    int workersCount;
    int workersCapacity;
    int maxTimeMs;
} Master;

int MasterInit(Master *master, const MasterConfig *config);

int MasterRun(
    Master *master,
    const void *data,
    size_t size,
    ClusterPacket *results
);

static inline void MasterPrepareEmpty(Master *master)
{
    master->listenSocketFd = -1;
    master->workers = NULL;
    master->workersCount = 0;
    master->workersCapacity = 0;
}

static inline void MasterDestroy(Master *master)
{
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
