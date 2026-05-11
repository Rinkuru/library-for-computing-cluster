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
        .max_time_ms = 10000,
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

    //TODO функция которая просто запускает мастер(сервер) и из этой функции не воходит пока не подключилось нужное количество рабочих узлов
    // если через 3 минуты не подключилось нужно кол-во рабочих узлов, то выдавть ошибку
    status = MasterInit(&master, &config);
    if (status != 0) {
        fprintf(stderr, "master init failed\n");
        return 1;
    }

    //TODO сделать разбиение задачи на необходимо кол-во рабочих узлов. Равномерно, пока что.
    tasks = calloc((size_t)config.required_workers, sizeof(IntegralTask));
    if (tasks == NULL) {
        fprintf(stderr, "unable to allocate tasks\n");
        MasterDestroy(&master);
        return 1;
    }

    double length = task.end - task.begin;
    for (int i = 0; i < config.required_workers; ++i) {
        tasks[i].begin = task.begin + length * (double)i / (double)config.required_workers;
        tasks[i].end = task.begin + length * (double)(i + 1) / (double)config.required_workers;
        tasks[i].eps = task.eps / (double)config.required_workers;
    }
    
    //TODO в эту функцию надо передавать массив из количества узлов и возвращать так же массив с результатами которые относятся к определённому узлу
    //Так же надо учесть что могут быть ошибки и надо их как-то возвращать
    workerResults = calloc((size_t)config.required_workers, sizeof(IntegralResult));
    if (workerResults == NULL) {
        fprintf(stderr, "unable to allocate worker results\n");
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    status = MasterRun(&master, tasks, workerResults);
    if (status != 0) {
        fprintf(stderr, "master run failed\n");
        free(workerResults);
        free(tasks);
        MasterDestroy(&master);
        return 1;
    }

    //TODO сделай суммирование результатов которые вернёт MasterRun чтобы выдать окончательный ответ по вычислению интеграла
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
