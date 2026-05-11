#define _GNU_SOURCE

#include "worker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
    size_t threadI;
    Method method;
    Func f;
    double begin;
    double end;
    double eps;
    double result;
} WorkerThreadArgs;

static void WorkerPrepareEmpty(Worker *worker) {
    worker->socketFd = -1;
    worker->task.begin = 0.0;
    worker->task.end = 0.0;
    worker->task.eps = 0.0;
    worker->result.value = 0.0;
    worker->resources.threads = 0;
    worker->resources.cores = 0;
}

static int WriteAll(int socketFd, const void *buffer, size_t size) {
    const char *data = (const char *)buffer;
    size_t offset = 0U;

    while (offset < size) {
        ssize_t written = write(socketFd, data + offset, size - offset);
        if (written == -1) {
            if (errno == EINTR) {
                continue;
            }

            return 1;
        }

        if (written == 0) {
            return 1;
        }

        offset += (size_t)written;
    }

    return 0;
}

static int ReadAll(int socketFd, void *buffer, size_t size) {
    char *data = (char *)buffer;
    size_t offset = 0U;

    while (offset < size) {
        ssize_t bytesRead = read(socketFd, data + offset, size - offset);
        if (bytesRead == -1) {
            if (errno == EINTR) {
                continue;
            }

            return 1;
        }

        if (bytesRead == 0) {
            return 1;
        }

        offset += (size_t)bytesRead;
    }

    return 0;
}

static void *WorkerThreadFunc(void *threadArgs) {
    WorkerThreadArgs *args = (WorkerThreadArgs *)threadArgs;
    if (args == NULL || args->method == NULL || args->f == NULL) {
        return NULL;
    }

    args->result = args->method(args->f, args->begin, args->end, args->eps);

    return NULL;
}

void hello_worker(void) {
    printf("Hello, i am worker\n");
}

int WorkerInit(
    Worker *worker,
    const WorkerConfig *config,
    const WorkerResources *resources) {
    if (worker == NULL) {
        return 1;
    }

    WorkerPrepareEmpty(worker);

    if (config == NULL ||
        resources == NULL ||
        config->host == NULL ||
        config->port <= 0 ||
        resources->threads <= 0 ||
        resources->cores <= 0) {
        return 1;
    }

    char port[16];
    int ret = snprintf(port, sizeof(port), "%d", config->port);
    if (ret <= 0 || (size_t)ret >= sizeof(port)) {
        return 1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addresses = NULL;
    ret = getaddrinfo(config->host, port, &hints, &addresses);
    if (ret != 0) {
        return 1;
    }

    int socketFd = -1;
    for (struct addrinfo *addr = addresses; addr != NULL; addr = addr->ai_next) {
        socketFd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (socketFd == -1) {
            continue;
        }

        if (connect(socketFd, addr->ai_addr, addr->ai_addrlen) == 0) {
            break;
        }

        close(socketFd);
        socketFd = -1;
    }

    freeaddrinfo(addresses);

    if (socketFd == -1) {
        return 1;
    }

    worker->socketFd = socketFd;
    worker->resources = *resources;

    int yes = 1;
    if (setsockopt(worker->socketFd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        WorkerDestroy(worker);
        return 1;
    }

    if (ReadAll(worker->socketFd, &worker->task, sizeof(worker->task)) != 0) {
        WorkerDestroy(worker);
        return 1;
    }

    return 0;
}

int WorkerRun(Worker *worker, Method method, Func f) {
    if (worker == NULL ||
        method == NULL ||
        f == NULL ||
        worker->resources.threads <= 0 ||
        worker->resources.cores <= 0) {
        return 1;
    }

    pthread_t *tids = calloc((size_t)worker->resources.threads, sizeof(pthread_t));
    WorkerThreadArgs *args = calloc((size_t)worker->resources.threads, sizeof(WorkerThreadArgs));
    if (tids == NULL || args == NULL) {
        free(tids);
        free(args);
        return 1;
    }

    const double length = worker->task.end - worker->task.begin;
    int status = 0;
    int createdThreads = 0;

    for (int i = 0; i < worker->resources.threads; ++i) {
        args[i].threadI = (size_t)i;
        args[i].method = method;
        args[i].f = f;
        args[i].begin = worker->task.begin + length * (double)i / (double)worker->resources.threads;
        args[i].end = worker->task.begin + length * (double)(i + 1) / (double)worker->resources.threads;
        args[i].eps = worker->task.eps / (double)worker->resources.threads;
        args[i].result = 0.0;

        pthread_attr_t threadAttributes;
        int ret = pthread_attr_init(&threadAttributes);
        if (ret != 0) {
            status = 1;
            break;
        }

        cpu_set_t assignedHarts;
        CPU_ZERO(&assignedHarts);
        CPU_SET((size_t)i % (size_t)worker->resources.cores, &assignedHarts);

        ret = pthread_attr_setaffinity_np(
            &threadAttributes,
            sizeof(cpu_set_t),
            &assignedHarts);
        if (ret != 0) {
            pthread_attr_destroy(&threadAttributes);
            status = 1;
            break;
        }

        ret = pthread_create(&tids[i], &threadAttributes, WorkerThreadFunc, &args[i]);
        pthread_attr_destroy(&threadAttributes);
        if (ret != 0) {
            status = 1;
            break;
        }

        createdThreads += 1;
    }

    worker->result.value = 0.0;
    for (int i = 0; i < createdThreads; ++i) {
        int ret = pthread_join(tids[i], NULL);
        if (ret != 0) {
            status = 1;
        }

        worker->result.value += args[i].result;
    }

    free(tids);
    free(args);

    return status;
}

int WorkerSendResult(Worker *worker) {
    if (worker == NULL || worker->socketFd < 0) {
        return 1;
    }

    return WriteAll(worker->socketFd, &worker->result, sizeof(worker->result));
}

void WorkerDestroy(Worker *worker) {
    if (worker == NULL) {
        return;
    }

    if (worker->socketFd >= 0) {
        close(worker->socketFd);
    }

    WorkerPrepareEmpty(worker);
}
