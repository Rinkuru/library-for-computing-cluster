#include "master.h"

#include <stdio.h>

void hello_master(void) {
    printf("Hello, i am master\n");
}

int MasterComputeIntegral(
    const MasterConfig *config,
    const IntegralTask *task,
    IntegralResult *result) {
        result->value = 0;
        return 0;
}
