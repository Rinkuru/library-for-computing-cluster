#include "worker.h"

#include <stdio.h>
#include <stdlib.h>

static double f(double x)
{
    return 4.0 / (1.0 + x * x);
}

int main(void)
{
    hello_worker();
    
    WorkerConfig config = {
        .host = "127.0.0.1",
        .port = 1337,
        .cores = 2,
        .maxTime= 10000,
    };

    int status = WorkerRun(&config, f);
    if (status != 0) {
        fprintf(stderr, "worker failed\n");
        return 1;
    }

    return 0;
}
