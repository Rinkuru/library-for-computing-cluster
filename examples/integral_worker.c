#define _GNU_SOURCE

#include "worker.h"
#include "integral.h"
#include "integral_example.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static IntegralMethod integral_method = LongMethod2;
static Func integral_func = SimpleFunc;

static ClusterPacket *MakeIntegralResultPacket(double value) {
    ClusterPacket *packet = malloc(sizeof(*packet));
    if (packet == NULL) {
        return NULL;
    }

    IntegralResult *result = malloc(sizeof(*result));
    if (result == NULL) {
        free(packet);
        return NULL;
    }

    result->value = value;
    packet->data = result;
    packet->size = sizeof(*result);
    return packet;
}

static void *RunIntegralTask(void *data, size_t size) {
    if (data == NULL || size != sizeof(ClusterThreadTask)) {
        return NULL;
    }

    ClusterThreadTask *threadTask = (ClusterThreadTask *)data;
    if (threadTask->data == NULL ||
        threadTask->size != sizeof(IntegralTask) ||
        threadTask->threadsCount == 0U ||
        threadTask->threadIndex >= threadTask->threadsCount) {
        return NULL;
    }

    IntegralTask task;
    memcpy(&task, threadTask->data, sizeof(task));

    double length = task.end - task.begin;
    double begin = task.begin + length * (double)threadTask->threadIndex / (double)threadTask->threadsCount;
    double end = task.begin + length * (double)(threadTask->threadIndex + 1U) / (double)threadTask->threadsCount;
    double eps = task.eps / (double)threadTask->threadsCount;

    double value = integral_method(integral_func, begin, end, eps);
    return MakeIntegralResultPacket(value);
}

void ParseArgs(int argc, char **argv, WorkerConfig *config, WorkerResources *resources) {
    char *timeout = "--timeout";
    char *threads = "--threads";
    char *cores = "--cores";
    char *firstCore = "--first-core";
    char *method = "--method";
    char *hardFunc = "--hardFunc";
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], method) == 0) {
                integral_method = LongMethod;
                continue;
            }
            if (strcmp(argv[i], hardFunc) == 0) {
                integral_func = HardFunc;
                continue;
            }
            if (strcmp(argv[i], timeout) == 0 && i + 1 < argc)
                config->max_time = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], threads) == 0 && i + 1 < argc)
                resources->threads = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], cores) == 0 && i + 1 < argc)
                resources->cores = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], firstCore) == 0 && i + 1 < argc)
                resources->firstCore = (int)strtoul(argv[i+1], NULL, 10);
        }
    }
}

int main(int argc, char **argv) {
    hello_worker();
    
    WorkerConfig config = {
        .host = "127.0.0.1",
        .port = 1337,
        .max_time = 18000,
        .method = RunIntegralTask,
    };
    Worker worker;
    int status = 0;

    WorkerResources resources = {
        .threads = 2,
        .cores = 1,
        .firstCore = 0,
    };

    ParseArgs(argc, argv, &config, &resources);

    status = WorkerInit(&worker, &config, &resources);
    if (status != 0) {
        fprintf(stderr, "worker init failed\n");
        return 1;
    }

    status = WorkerRun(&worker, config.method);
    if (status != 0) {
        fprintf(stderr, "worker run failed\n");
        WorkerDestroy(&worker);
        return 1;
    }

    status = WorkerSendResult(&worker);
    if (status != 0) {
        fprintf(stderr, "worker send result failed\n");
        WorkerDestroy(&worker);
        return 1;
    }

    WorkerDestroy(&worker);

    return 0;
}
