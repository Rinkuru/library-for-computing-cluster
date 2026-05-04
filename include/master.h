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

int MasterComputeIntegral(
    const MasterConfig *config,
    const IntegralTask *task,
    IntegralResult *result
);

#endif
