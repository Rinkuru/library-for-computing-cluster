#define _GNU_SOURCE

#include "worker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
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

static long NowMs(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "[NowMs] Unable NowMs\n");
        return -1;
    }

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void SleepMs(long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000L;
    ts.tv_nsec = (milliseconds % 1000L) * 1000000L;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static int WriteAll(int socketFd, const void *buffer, size_t size) {
    const char *data = (const char *)buffer;
    size_t offset = 0U;

    while (offset < size) {
        ssize_t written = send(socketFd, data + offset, size - offset, 0);
        if (written == -1) {
            if (errno == EINTR) continue;
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
        ssize_t bytesRead = recv(socketFd, data + offset, size - offset, 0);
        if (bytesRead == -1) {
            if (errno == EINTR) continue;
            return 1;
        }

        offset += (size_t)bytesRead;
    }

    return 0;
}

static int TryConnectToMaster(const struct addrinfo *address, int *socketFd) {
    int currentSocket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (currentSocket == -1) {
        fprintf(stderr, "[WorkerInit] Unable to create socket\n");
        return 1;
    }

    if (connect(currentSocket, address->ai_addr, address->ai_addrlen) == 0) {
        *socketFd = currentSocket;
        return 0;
    }

    int connectErrno = errno;
    close(currentSocket);

    if (connectErrno == ECONNREFUSED) {
        return 2;
    }

    fprintf(stderr, "[WorkerInit] Unable to connect to master\n");
    return 1;
}

static int ConnectToMaster(
    const WorkerConfig *config,
    const struct addrinfo *address,
    int *socketFd) {
    long now = NowMs();
    if (now < 0) {
        return 1;
    }

    long deadline = now + (long)config->maxTime;

    while (true) {
        int status = TryConnectToMaster(address, socketFd);
        if (status == 0) return 0;

        if (status == 1) return 1;

        now = NowMs();
        if (now >= deadline) {
            fprintf(stderr, "[WorkerInit] timeout while waiting for master\n");
            return 1;
        }

        printf("Wait for master to start\n");
        SleepMs(500L);
    }
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
        config->maxTime <= 0 ||
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
    if (ret != 0) return 1;

    if (addresses->ai_next != NULL) {
        fprintf(stderr, "[WorkerInit] Ambiguous result of getaddrinfo\n");
        freeaddrinfo(addresses);
        return 1;
    }

    int socketFd = -1;
    ret = ConnectToMaster(config, addresses, &socketFd);
    if (ret != 0 || socketFd == -1) return 1;

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

    freeaddrinfo(addresses);
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
    if (worker == NULL || worker->socketFd < 0) return 1;

    return WriteAll(worker->socketFd, &worker->result, sizeof(worker->result));
}

void WorkerDestroy(Worker *worker) {
    if (worker == NULL) return;

    if (worker->socketFd >= 0) {
        close(worker->socketFd);
    }

    WorkerPrepareEmpty(worker);
}
