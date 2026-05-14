#include "worker.h"
#include "integral.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define LONG_METHOD_STEPS 200000000L

static double SimpleFunc(double x)
{
    return (double)4.0 / (1.0 + x*x); 
}

static double HardFunc(double x)
{
    return 2.0 + sin(2000000.0 * x);
}

double LongMethod2(Func f, double a, double b, double eps) {
    (void)eps;

    const double length = b - a;
    if (length <= 0.0) {
        return 0.0;
    }

    long steps = (long)((double)LONG_METHOD_STEPS * length);
    if (steps < 1L) {
        steps = 1L;
    }

    const double h = length / (double)steps;
    double sum = 0.0;
    for (long i = 0; i < steps; ++i) {
        double x = a + ((double)i + 0.5) * h;
        sum += f(x);
    }

    return sum * h;
}

double LongMethod(Func f, double a, double b, double eps) {
    (double)eps;
    double sum = 0;
    for (long i = a; i < b; ++i) {
        sum += f(i);
    }
    return sum;
}

void ParseArgs(int argc, char **argv, WorkerConfig *config, WorkerResources *resources)
{
    char *timeout = "--timeout";
    char *threads = "--threads";
    char *cores = "--cores";
    char *firstCore = "--first-core";
    char *method = "--method";
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], method) == 0)
                config->method = LongMethod;
            if (strcmp(argv[i], timeout) == 0)
                config->max_time = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], threads) == 0)
                resources->threads = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], cores) == 0)
                resources->cores = (int)strtoul(argv[i+1], NULL, 10);
            if (strcmp(argv[i], firstCore) == 0)
                resources->firstCore = (int)strtoul(argv[i+1], NULL, 10);
        }
    }
}

int main(int argc, char **argv)
{
    hello_worker();
    
    WorkerConfig config = {
        .host = "127.0.0.1",
        .port = 1337,
        .max_time = 18000,
        .method = SlowSimpson,
    };
    Worker worker;
    int status = 0;

    //Выделить ядра и потоки, которые будут доступны этому процессу "рабочего узла"
    WorkerResources resources = {
        .threads = 2,
        .cores = 1,
        .firstCore = 0,
    };

    ParseArgs(argc, argv, &config, &resources);

    //подключение к серверу, и сохранение у себя параметров сколько потоков надо будет создать
    //а так же обработка ошибки если не получилось подключиться
    status = WorkerInit(&worker, &config, &resources);
    if (status != 0) {
        fprintf(stderr, "worker init failed\n");
        return 1;
    }


    //передача задания на исполнение тут мы должны передать функции для исполнения. И как-то взять от мастера eps и границы подсчётов и посчтиать на своей вычислительной мощности (сколько там выделили поток и ядер), а потом вернуть то что посчитали
    status = WorkerRun(&worker, config.method, SimpleFunc);
    if (status != 0) {
        fprintf(stderr, "worker run failed\n");
        WorkerDestroy(&worker);
        return 1;
    }

    //сделать ещё функцию для worker.c такую что вернёт результат мастеру. Это должна быть именно отдельная функция
    status = WorkerSendResult(&worker);
    if (status != 0) {
        fprintf(stderr, "worker send result failed\n");
        WorkerDestroy(&worker);
        return 1;
    }

    WorkerDestroy(&worker);

    return 0;
}
