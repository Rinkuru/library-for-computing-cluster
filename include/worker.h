#ifndef WORKER_H
#define WORKER_H

void hello_worker(void);

typedef double (*Func)(double x);

typedef struct {
    const char *host;
    int port;
    int cores;
    int maxTime;
} WorkerConfig;

int WorkerRun(const WorkerConfig *config, Func f);

#endif
