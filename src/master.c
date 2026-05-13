#define _GNU_SOURCE

#include "master.h"
#include "master_multiplexing.c"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NUM_HARDWARE_THREADS 4U

static int MasterPrepare(Master *master, const MasterConfig *config) {
    MasterPrepareEmpty(master);

    master->workers = calloc((size_t)config->required_workers, sizeof(MasterWorker));
    if (master->workers == NULL) {
        fprintf(stderr, "[MasterInit] Unable to calloc\n");
        return 1;
    }

    master->workersCapacity = config->required_workers;
    for (int i = 0; i < config->required_workers; ++i) {
        master->workers[i].socketFd = -1;
    }
    return 0;
}

static long NowMs(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fprintf(stderr, "[NowMs] Unable HowMs\n");
        return -1;
    }

    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
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

static void *MasterThreadFunc(void *threadArgs) {
    MasterThreadArgs *args = (MasterThreadArgs *)threadArgs;
    if (args == NULL || args->result == NULL) {
        return NULL;
    }

    args->status = WriteAll(args->socketFd, &args->task, sizeof(args->task));
    if (args->status != 0) {
        return NULL;
    }

    args->status = ReadAll(args->socketFd, args->result, sizeof(*args->result));

    return NULL;
}

void hello_master(void) {
    printf("Hello, i am master\n");
}

int MasterInit(Master *master, const MasterConfig *config) {

    // Маскируем все остальные сигналы на время вызова обработчика сигнала. то что называется init_shutdown_control() из server-common.h

    if (master == NULL || config == NULL || config->port <= 0 || config->required_workers <= 0) {
        fprintf(stderr, "[MasterInit] NULL config\n");
        return 1;
    }

    if (MasterPrepare(master, config)) return 1;

    if (MasterInitListenSocket(master, config)) return 1;

    long deadline = NowMs();
    deadline += config->max_time_ms;

    while (true) {

        if (master->workersCount == config->required_workers) {
            break;
        }

        long now = NowMs();
        if (now < 0 || now >= deadline) {
            fprintf(stderr, "[MasterInit] deadline expired\n");
            MasterDestroy(master);
            return 1;
        }

        struct pollfd pollFd;
        pollFd.fd = master->listenSocketFd;
        pollFd.events = POLLIN;
        pollFd.revents = 0;

        int timeout = (int)(deadline - now);
        int pollResult = poll(&pollFd, 1U, timeout);
        if (pollResult == -1) {
            fprintf(stderr, "[MasterInit] Unable to poll-wait for data on descriptors\n");
            MasterDestroy(master);
            return 1;
        }

        if (pollResult == 0) {
            fprintf(stderr, "[MasterInit] timeout connect\n");
            MasterDestroy(master);
            return 1;
        }

        if ((pollFd.revents & POLLIN) == 0) {
            fprintf(stderr, "[MasterInit] Unebale POLLIN");
            MasterDestroy(master);
            return 1;
        }

        printf("Wait for client to connect\n");

        int workerSocket = accept(master->listenSocketFd, NULL, NULL);
        if (workerSocket == -1) {
            fprintf(stderr, "[MasterInit] Unable to accept() connection on a socket\n");
            MasterDestroy(master);
            return 1;
        }

        int yes = 1;
        if (setsockopt(workerSocket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
            fprintf(stderr, "[server_accept_connection_request] Unable to enable TCP_NODELAY socket option");
            close(workerSocket);
            MasterDestroy(master);
            return 1;
        }

        master->workers[master->workersCount].socketFd = workerSocket;
        master->workersCount += 1;
        printf("Client connected: %d\n", master->workersCount);
    }

    return 0;
}

int MasterRun(
    const Master *master,
    const IntegralTask *tasks,
    IntegralResult *results) {
    if (master == NULL || tasks == NULL || results == NULL || master->workersCount <= 0) {
        fprintf(stderr, "[MasterRun] NULL config\n");
        return 1;
    }

    pthread_t *tids = calloc((size_t)master->workersCount, sizeof(pthread_t));
    MasterThreadArgs *args = calloc((size_t)master->workersCount, sizeof(MasterThreadArgs));
    if (tids == NULL || args == NULL) {
        fprintf(stderr, "[MasterRun] Unable to calloc\n");
        free(tids);
        free(args);
        return 1;
    }

    int status = 0;
    int createdThreads = 0;
    for (int i = 0; i < master->workersCount; ++i) {
        args[i].workerI = i;
        args[i].socketFd = master->workers[i].socketFd;
        args[i].task = tasks[i];
        args[i].result = &results[i];
        args[i].status = 0;

        pthread_attr_t threadAttributes;
        int ret = pthread_attr_init(&threadAttributes);
        if (ret != 0) {
            status = 1;
            break;
        }

        cpu_set_t assignedHarts;
        CPU_ZERO(&assignedHarts);
        CPU_SET((size_t)i % NUM_HARDWARE_THREADS, &assignedHarts);

        ret = pthread_attr_setaffinity_np(
            &threadAttributes,
            sizeof(cpu_set_t),
            &assignedHarts);
        if (ret != 0) {
            pthread_attr_destroy(&threadAttributes);
            status = 1;
            break;
        }

        ret = pthread_create(&tids[i], &threadAttributes, MasterThreadFunc, &args[i]);
        pthread_attr_destroy(&threadAttributes);
        if (ret != 0) {
            status = 1;
            break;
        }

        createdThreads += 1;
    }

    for (int i = 0; i < createdThreads; ++i) {
        int ret = pthread_join(tids[i], NULL);
        if (ret != 0 || args[i].status != 0) {
            status = 1;
        }
    }

    free(tids);
    free(args);

    return status;
}
