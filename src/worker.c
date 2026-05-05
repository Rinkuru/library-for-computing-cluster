#include "worker.h"
#include "integral.h"

#include <stdio.h>

void hello_worker(void) {
    printf("Hello, i am worker\n");
}

int WorkerRun(const WorkerConfig *config, Func f) {
    if (config == 0 || f == 0) {
        return 1;
    }

    double eps = 0.000000000001;
    printf("slow = %.16f\n", SlowSimpson(f, 0, 1, eps));

    return 0;
}
