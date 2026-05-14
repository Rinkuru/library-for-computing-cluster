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

static void WorkerPrepareEmpty(Worker *worker)
{
    worker->socketFd = -1;
    worker->task.begin = 0.0;
    worker->task.end = 0.0;
    worker->task.eps = 0.0;
    worker->result.value = 0.0;
    worker->resources.threads = 0;
    worker->resources.cores = 0;
    worker->resources.firstCore = 0;
}

static void SleepMs(long milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000L;
    ts.tv_nsec = (milliseconds % 1000L) * 1000000L;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static int WriteAll(int socketFd, const void *buffer, size_t size)
{
    const char *data = (const char *)buffer;
    size_t offset = 0U;

    while (offset < size) {
        ssize_t written = send(socketFd, data + offset, size - offset, 0);
        if (written == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[WorkerIO] Unable to send data\n");
            return 1;
        }

        if (written == 0) {
            fprintf(stderr, "[WorkerIO] Socket sent zero bytes\n");
            return 1;
        }

        offset += (size_t)written;
    }

    return 0;
}

static int ReadAll(int socketFd, void *buffer, size_t size)
{
    char *data = (char *)buffer;
    size_t offset = 0U;

    while (offset < size) {
        ssize_t bytesRead = recv(socketFd, data + offset, size - offset, 0);
        if (bytesRead == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[WorkerIO] Unable to receive data\n");
            return 1;
        }

        if (bytesRead == 0) {
            fprintf(stderr, "[WorkerIO] Connection closed while receiving data\n");
            return 1;
        }

        offset += (size_t)bytesRead;
    }

    return 0;
}

static int TryConnectToMaster(const struct addrinfo *address, int *socketFd)
{
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

static int ConnectToMaster(const struct addrinfo *address, int *socketFd)
{
    while (true) {
        int status = TryConnectToMaster(address, socketFd);
        if (status == 0) return 0;
        if (status == 1) {
            fprintf(stderr, "[WorkerInit] Unable to connect to master\n");
            return 1;
        }
        printf("Wait for master to start\n");
        SleepMs(500L);
    }
}

static void *WorkerThreadFunc(void *threadArgs)
{
    WorkerThreadArgs *args = (WorkerThreadArgs *)threadArgs;
    if (args == NULL || args->method == NULL || args->f == NULL) {
        return NULL;
    }

    args->result = args->method(args->f, args->begin, args->end, args->eps);

    return NULL;
}

void hello_worker(void)
{
    printf("Hello, i am worker\n");
}

int WorkerInit(Worker *worker, const WorkerConfig *config, const WorkerResources *resources)
{
    if (worker == NULL) {
        fprintf(stderr, "[WorkerInit] NULL worker\n");
        return 1;
    }

    WorkerPrepareEmpty(worker);

    if (config == NULL ||
        resources == NULL ||
        config->host == NULL ||
        config->port <= 0 ||
        config->max_time <= 0 ||
        resources->threads <= 0 ||
        resources->cores <= 0 ||
        resources->firstCore < 0 ||
        resources->firstCore + resources->cores > CPU_SETSIZE) {
        fprintf(stderr, "[WorkerInit] NULL config\n");
        return 1;
    }

    char port[16];
    int ret = snprintf(port, sizeof(port), "%d", config->port);
    if (ret <= 0 || (size_t)ret >= sizeof(port)) {
        fprintf(stderr, "[WorkerInit] Unable to format port\n");
        return 1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *addresses = NULL;
    ret = getaddrinfo(config->host, port, &hints, &addresses);
    if (ret != 0) {
        fprintf(stderr, "[WorkerInit] Unable to resolve master address\n");
        return 1;
    }

    if (addresses->ai_next != NULL) {
        fprintf(stderr, "[WorkerInit] Ambiguous result of getaddrinfo\n");
        freeaddrinfo(addresses);
        return 1;
    }

    int socketFd = -1;
    ret = ConnectToMaster(addresses, &socketFd);
    if (ret != 0 || socketFd == -1) {
        fprintf(stderr, "[WorkerInit] Unable to connect to master before timeout\n");
        freeaddrinfo(addresses);
        return 1;
    }

    worker->socketFd = socketFd;
    worker->resources = *resources;
    worker->maxTime = config->max_time;

    int yes = 1;
    if (setsockopt(worker->socketFd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        fprintf(stderr, "[WorkerInit] Unable to enable TCP_NODELAY\n");
        freeaddrinfo(addresses);
        WorkerDestroy(worker);
        return 1;
    }

    if (ReadAll(worker->socketFd, &worker->task, sizeof(worker->task)) != 0) {
        fprintf(stderr, "[WorkerInit] Unable to read task from master\n");
        freeaddrinfo(addresses);
        WorkerDestroy(worker);
        return 1;
    }

    freeaddrinfo(addresses);
    return 0;
}

int WorkerRun(Worker *worker, Method method, Func f)
{
    if (worker == NULL ||
        method == NULL ||
        f == NULL ||
        worker->resources.threads <= 0 ||
        worker->resources.cores <= 0 ||
        worker->resources.firstCore < 0 ||
        worker->resources.firstCore + worker->resources.cores > CPU_SETSIZE) {
        fprintf(stderr, "[WorkerRun] NULL config\n");
        return 1;
    }

    pthread_t *tids = calloc((size_t)worker->resources.threads, sizeof(pthread_t));
    WorkerThreadArgs *args = calloc((size_t)worker->resources.threads, sizeof(WorkerThreadArgs));
    if (tids == NULL || args == NULL) {
        fprintf(stderr, "[WorkerRun] Unable to allocate thread resources\n");
        free(tids);
        free(args);
        return 1;
    }

    const double length = worker->task.end - worker->task.begin;
    int status = 0;
    int createdThreads = 0;

    long startTime = NowMs();
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
            fprintf(stderr, "[WorkerRun] Unable to initialize thread attributes\n");
            status = 1;
            break;
        }

        cpu_set_t assignedHarts;
        CPU_ZERO(&assignedHarts);
        CPU_SET(
            (size_t)(worker->resources.firstCore + i % worker->resources.cores),
            &assignedHarts);

        ret = pthread_attr_setaffinity_np(
            &threadAttributes,
            sizeof(cpu_set_t),
            &assignedHarts);
        if (ret != 0) {
            fprintf(stderr, "[WorkerRun] Unable to set thread affinity\n");
            pthread_attr_destroy(&threadAttributes);
            status = 1;
            break;
        }

        ret = pthread_create(&tids[i], &threadAttributes, WorkerThreadFunc, &args[i]);
        pthread_attr_destroy(&threadAttributes);
        if (ret != 0) {
            fprintf(stderr, "[WorkerRun] Unable to create thread\n");
            status = 1;
            break;
        }
        
        if (NowMs() - startTime > worker->maxTime) {
            fprintf(stderr, "[WorkerRun] Deadline the time limit\n");
            status = 1;
            break;
        }

        createdThreads += 1;
    }

    worker->result.value = 0.0;
    for (int i = 0; i < createdThreads; ++i) {
        int ret = pthread_join(tids[i], NULL);
        if (ret != 0) {
            fprintf(stderr, "[WorkerRun] Unable to join thread\n");
            status = 1;
        }

        if (NowMs() - startTime > worker->maxTime) {
            fprintf(stderr, "[WorkerRun] Deadline the time limit\n");
            status = 1;
            break;
        }

        worker->result.value += args[i].result;
    }

    free(tids);
    free(args);

    return status;
}

int WorkerSendResult(Worker *worker)
{
    if (worker == NULL || worker->socketFd < 0) {
        fprintf(stderr, "[WorkerSendResult] NULL worker\n");
        return 1;
    }

    if (WriteAll(worker->socketFd, &worker->result, sizeof(worker->result)) != 0) {
        fprintf(stderr, "[WorkerSendResult] Unable to send result to master\n");
        return 1;
    }

    return 0;
}

void WorkerDestroy(Worker *worker)
{
    if (worker == NULL) return;

    if (worker->socketFd >= 0) {
        close(worker->socketFd);
    }

    WorkerPrepareEmpty(worker);
}
