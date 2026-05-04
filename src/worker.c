#include "worker.h"

#include <stdio.h>

void hello_worker(void) {
    printf("Hello, i am worker\n");
}

int WorkerRun(const WorkerConfig *config, Func f) {
    if (config == 0 || f == 0) {
        return 1;
    }

    return 0;
}
