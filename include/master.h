#ifndef MASTER_H
#define MASTER_H

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

void MasterDestroy(Master *master);

int MasterComputeIntegral(
    const MasterConfig *config,
    const IntegralTask *task,
    IntegralResult *result
);

#endif
