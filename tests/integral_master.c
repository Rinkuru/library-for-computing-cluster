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

    int status = MasterComputeIntegral(&config, &task, &result);
    if (status != 0) {
        fprintf(stderr, "master failed\n");
        return 1;
    }

    printf("Integral: %.10f\n", result.value);

    return 0;
}
