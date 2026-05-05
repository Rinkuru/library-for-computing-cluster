#include "worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static double SimpleFunc(double x) {
    return (double)4.0 / (1.0 + x*x); 
}

static double HardFunc(double x) {
    return 2.0 + sin(2000000.0 * x);
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

    int status = WorkerRun(&config, SimpleFunc);
    if (status != 0) {
        fprintf(stderr, "worker failed\n");
        return 1;
    }

    return 0;
}
