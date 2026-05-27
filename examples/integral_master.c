#include "master.h"
#include "integral_example.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ParseArgs(int argc, char **argv, MasterConfig *config, IntegralTask *task, int *workerThreads)
{
    char *required_workers = "--workers";
    char *timeout = "--timeout";
    char *threads = "--threads";
    char *end = "--end";
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], required_workers) == 0 && i + 1 < argc)
                config->required_workers = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], timeout) == 0 && i + 1 < argc)
                config->max_time_ms = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], threads) == 0 && i + 1 < argc)
                *workerThreads = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], end) == 0)
                task->end = 10000000000;
        }
    }
}

static void FreeWorkerResults(ClusterPacket *workerResults, int count)
{
    if (workerResults == NULL) return;

    for (int i = 0; i < count; ++i) {
        free(workerResults[i].data);
        workerResults[i].data = NULL;
        workerResults[i].size = 0U;
    }
}

static int AddPackedResults(const ClusterPacket *packet, IntegralResult *result)
{
    if (packet == NULL ||
        result == NULL ||
        packet->data == NULL ||
        packet->size < ClusterSizeHeaderSize) {
        return 1;
    }

    const unsigned char *bytes = (const unsigned char *)packet->data;
    uint64_t count = ClusterDecodeSize(bytes);
    size_t offset = ClusterSizeHeaderSize;

    for (uint64_t i = 0U; i < count; ++i) {
        if (packet->size - offset < ClusterSizeHeaderSize) {
            return 1;
        }

        uint64_t itemSize = ClusterDecodeSize(bytes + offset);
        offset += ClusterSizeHeaderSize;
        if (itemSize != sizeof(IntegralResult) ||
            itemSize > (uint64_t)SIZE_MAX ||
            packet->size - offset < (size_t)itemSize) {
            return 1;
        }

        IntegralResult workerResult;
        memcpy(&workerResult, bytes + offset, sizeof(workerResult));
        result->value += workerResult.value;
        offset += (size_t)itemSize;
    }

    return offset == packet->size ? 0 : 1;
}

int main(int argc, char **argv)
{
    hello_master();
    
    MasterConfig config = {
        .host = "127.0.0.1",
        .port = 1337,
        .required_workers = 2,
        .max_time_ms = 18000,
    };

    IntegralTask task = {
        .begin = 0.0,
        .end = 1.0,
        .eps = 1e-6,
    };

    int workerThreads = IntegralDefaultWorkerThreads;
    ParseArgs(argc, argv, &config, &task, &workerThreads);
    if (workerThreads <= 0) {
        fprintf(stderr, "worker threads must be positive\n");
        return 1;
    }

    IntegralResult result;
    Master master;
    IntegralTask *tasks = NULL;
    ClusterPacket *workerResults = NULL;
    int status = 0;

    status = MasterInit(&master, &config);
    if (status != 0) {
        fprintf(stderr, "master init failed\n");
        return 1;
    }

    size_t totalTasks = (size_t)config.required_workers * (size_t)workerThreads;
    tasks = calloc(totalTasks, sizeof(IntegralTask));
    if (tasks == NULL) {
        fprintf(stderr, "unable to allocate tasks\n");
        MasterDestroy(&master);
        return 1;
    }

    double length = task.end - task.begin;
    for (size_t i = 0U; i < totalTasks; ++i) {
        tasks[i].begin = task.begin + length * (double)i / (double)totalTasks;
        tasks[i].end = task.begin + length * (double)(i + 1U) / (double)totalTasks;
        tasks[i].eps = task.eps / (double)totalTasks;
    }
    
    workerResults = calloc((size_t)config.required_workers, sizeof(ClusterPacket));
    if (workerResults == NULL) {
        fprintf(stderr, "unable to allocate worker results\n");
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    status = MasterRun(&master, tasks, sizeof(*tasks) * totalTasks, workerResults);
    if (status != 0) {
        fprintf(stderr, "master run failed\n");
        FreeWorkerResults(workerResults, config.required_workers);
        free(workerResults);
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    result.value = 0.0;
    for (int i = 0; i < config.required_workers; ++i) {
        if (AddPackedResults(&workerResults[i], &result) != 0) {
            fprintf(stderr, "worker returned invalid result packet\n");
            FreeWorkerResults(workerResults, config.required_workers);
            free(workerResults);
            free(tasks);
            MasterDestroy(&master);
            return 1;
        }
    }

    printf("Integral: %.10f\n", result.value);

    FreeWorkerResults(workerResults, config.required_workers);
    free(workerResults);
    free(tasks);
    MasterDestroy(&master);

    return 0;
}
