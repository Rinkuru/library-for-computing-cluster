#include "master.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    hello_master();
    
    MasterConfig config = {
        .host = "127.0.0.1",
        .port = 1337,
        .required_workers = 1,
        .max_time_ms = 180000L,
    };

    IntegralTask task = {
        .begin = 0.0,
        .end = 1.0,
        .eps = 1e-6,
    };

    IntegralResult result;
    Master master;
    IntegralTask *tasks = NULL;
    IntegralResult *workerResults = NULL;
    int status = 0;

    //функция которая просто запускает мастер(сервер): создаёт listen socket и выделяет ресурсы
    status = MasterInit(&master, &config);
    if (status != 0) {
        fprintf(stderr, "master init failed\n");
        return 1;
    }

    tasks = calloc((size_t)config.required_workers, sizeof(IntegralTask));
    if (tasks == NULL) {
        fprintf(stderr, "unable to allocate tasks\n");
        MasterDestroy(&master);
        return 1;
    }

    //Равноемерное разбиение на задачи
    double length = task.end - task.begin;
    for (int i = 0; i < config.required_workers; ++i) {
        tasks[i].begin = task.begin + length * (double)i / (double)config.required_workers;
        tasks[i].end = task.begin + length * (double)(i + 1) / (double)config.required_workers;
        tasks[i].eps = task.eps / (double)config.required_workers;
    }
    
    workerResults = calloc((size_t)config.required_workers, sizeof(IntegralResult));
    if (workerResults == NULL) {
        fprintf(stderr, "unable to allocate worker results\n");
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    //Подключение рабочих узлов происходит в MasterRun
    status = MasterRun(&master, tasks, workerResults);
    if (status != 0) {
        fprintf(stderr, "master run failed\n");
        free(workerResults);
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    result.value = 0.0;
    for (int i = 0; i < config.required_workers; ++i) {
        result.value += workerResults[i].value;
    }

    printf("Integral: %.10f\n", result.value);

    free(workerResults);
    free(tasks);
    MasterDestroy(&master);

    return 0;
}
