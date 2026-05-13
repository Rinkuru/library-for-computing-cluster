#define _GNU_SOURCE

#include "common.h"
#include "master.h"
#include "master_multiplexing.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef enum {
    TransferComplete = 0,
    TransferPending,
    TransferFailed
} TransferStatus;

static void MasterWorkerPrepareEmpty(MasterWorker *worker)
{
    worker->socketFd = -1;
    worker->state = MasterWorkerEmpty;
    worker->transferOffset = 0U;
}

static int MasterPrepare(Master *master, const MasterConfig *config)
{
    MasterPrepareEmpty(master);

    master->workers = calloc((size_t)config->required_workers, sizeof(MasterWorker));
    if (master->workers == NULL) {
        fprintf(stderr, "[MasterInit] Unable to calloc\n");
        return 1;
    }

    master->workersCapacity = config->required_workers;
    for (int i = 0; i < config->required_workers; ++i) {
        MasterWorkerPrepareEmpty(&master->workers[i]);
    }

    master->maxTimeMs = config->max_time_ms;

    return 0;
}

static int SetNonBlocking(int socketFd)
{
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) return 1;
    if (fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) == -1) return 1;
    return 0;
}

static TransferStatus WritePart(int socketFd, const void *buffer, size_t size, size_t *offset)
{
    const char *data = (const char *)buffer;
    while (*offset < size) {
        ssize_t written = send(socketFd, data + *offset, size - *offset, MSG_NOSIGNAL);
        if (written == -1) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return TransferPending;
            return TransferFailed;
        }

        if (written == 0) return TransferFailed;
        *offset += (size_t)written;
    }
    return TransferComplete;
}

static TransferStatus ReadPart(int socketFd, void *buffer, size_t size, size_t *offset)
{
    char *data = (char *)buffer;
    while (*offset < size) {
        ssize_t bytesRead = recv(socketFd, data + *offset, size - *offset, 0);
        if (bytesRead == -1) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return TransferPending;
            return TransferFailed;
        }

        if (bytesRead == 0) return TransferFailed;
        *offset += (size_t)bytesRead;
    }
    return TransferComplete;
}

static void CloseWorkerSocket(MasterWorker *worker)
{
    if (worker->socketFd >= 0) {
        close(worker->socketFd);
    }
    worker->socketFd = -1;
}

static void CloseAllWorkers(Master *master)
{
    for (int i = 0; i < master->workersCount; ++i) {
        CloseWorkerSocket(&master->workers[i]);
        if (master->workers[i].state != Done) {
            master->workers[i].state = Failed;
        }
    }
}

static int MasterAcceptWorker(Master *master)
{
    printf("Wait for client to connect\n");

    int workerSocket = accept(master->listenSocketFd, NULL, NULL);
    if (workerSocket == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
        fprintf(stderr, "[MasterRun] Unable to accept() connection on a socket\n");
        return 1;
    }

    int yes = 1;
    if (setsockopt(workerSocket, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        fprintf(stderr, "[MasterRun] Unable to enable TCP_NODELAY socket option\n");
        close(workerSocket);
        return 1;
    }

    if (SetNonBlocking(workerSocket) != 0) {  // переводит в неблокирующий режим
        fprintf(stderr, "[MasterRun] Unable to set worker socket nonblocking\n");
        close(workerSocket);
        return 1;
    }

    MasterWorker *worker = &master->workers[master->workersCount];
    worker->socketFd = workerSocket;
    worker->state = SendTask;
    worker->transferOffset = 0U;

    master->workersCount += 1;
    printf("Client connected: %d\n", master->workersCount);

    return 0;
}

static void PreparePollFds(Master *master, struct pollfd *pollFds)
{
    if (master->workersCount < master->workersCapacity) {
        pollFds[0].fd = master->listenSocketFd;
        pollFds[0].events = POLLIN;
        pollFds[0].revents = 0;
    } else {
        pollFds[0].fd = -1;
        pollFds[0].events = 0;
        pollFds[0].revents = 0;
    }

    for (int i = 0; i < master->workersCapacity; ++i) {
        struct pollfd *pollFd = &pollFds[1 + i];
        pollFd->fd = -1;
        pollFd->events = 0;
        pollFd->revents = 0;

        if (i >= master->workersCount || master->workers[i].socketFd < 0) {
            continue;
        }

        switch (master->workers[i].state) {
        case SendTask:
            pollFd->fd = master->workers[i].socketFd;
            pollFd->events = POLLOUT;
            break;
        case ReadResult:
            pollFd->fd = master->workers[i].socketFd;
            pollFd->events = POLLIN;
            break;
        case MasterWorkerEmpty:
        case Done:
        case Failed:
            break;
        }
    }
}

void hello_master(void)
{
    printf("Hello, i am master\n");
}

int MasterInit(Master *master, const MasterConfig *config)
{
    if (master == NULL) {
        fprintf(stderr, "[MasterInit] NULL master\n");
        return 1;
    }

    if (master == NULL ||
        config == NULL ||
        config->port <= 0 ||
        config->required_workers <= 0) {
        fprintf(stderr, "[MasterInit] NULL config\n");
        return 1;
    }

    if (MasterPrepare(master, config)) return 1;

    if (MasterInitListenSocket(master, config)) return 1;

    return 0;
}

int MasterRun(Master *master,const IntegralTask *tasks, IntegralResult *results)
{
    if (master == NULL ||
        tasks == NULL ||
        results == NULL ||
        master->listenSocketFd < 0 ||
        master->workersCapacity <= 0) {
        fprintf(stderr, "[MasterRun] NULL config\n");
        return 1;
    }

    struct pollfd *pollFds = calloc((size_t)master->workersCapacity + 1U, sizeof(struct pollfd));
    if (pollFds == NULL) {
        fprintf(stderr, "[MasterRun] Unable to calloc\n");
        return 1;
    }

    int doneWorkers = 0;
    int status = 0;

    long startTime = NowMs();
    while (doneWorkers < master->workersCapacity) {
        PreparePollFds(master, pollFds);

        int pollResult = poll(pollFds, (nfds_t)master->workersCapacity + 1U, -1);
        if (pollResult == -1) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[MasterRun] Unable to poll-wait for data on descriptors\n");
            status = 1;
            break;
        }

        if ((pollFds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "[MasterRun] listen socket failed\n");
            status = 1;
            break;
        }

        printf("I'm waiting worker\n");
        if ((pollFds[0].revents & POLLIN) != 0) {
            if (MasterAcceptWorker(master) != 0) {
                status = 1;
                break;
            }
        }

        for (int i = 0; i < master->workersCount; ++i) {
            MasterWorker *worker = &master->workers[i];
            struct pollfd *pollFd = &pollFds[1 + i];
            short revents = pollFd->revents;

            if (pollFd->fd < 0 || revents == 0) continue;

            if ((revents & (POLLERR | POLLNVAL)) != 0) {
                fprintf(stderr, "[MasterRun] worker socket failed\n");
                worker->state = Failed;
                status = 1;
                break;
            }

            if (worker->state == SendTask) {
                if ((revents & POLLOUT) != 0) {
                    TransferStatus transferStatus = WritePart(worker->socketFd,
                                                              &tasks[i],
                                                              sizeof(tasks[i]),
                                                              &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to send task to worker\n");
                        worker->state = Failed;
                        status = 1;
                        break;
                    }

                    if (transferStatus == TransferComplete) {
                        worker->transferOffset = 0U;
                        worker->state = ReadResult;
                    }
                }

                if ((revents & POLLHUP) != 0 && worker->state == SendTask) {
                    fprintf(stderr, "[MasterRun] worker disconnected before task was sent\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }

                continue;
            }

            if (worker->state == ReadResult) {
                if ((revents & POLLIN) != 0) {
                    TransferStatus transferStatus = ReadPart(worker->socketFd,
                                                             &results[i],
                                                             sizeof(results[i]),
                                                             &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to read result from worker\n");
                        worker->state = Failed;
                        status = 1;
                        break;
                    }

                    if (transferStatus == TransferComplete) {
                        worker->transferOffset = 0U;
                        worker->state = Done;
                        doneWorkers += 1;
                    }
                }

                if ((revents & POLLHUP) != 0 && worker->state == ReadResult) {
                    fprintf(stderr, "[MasterRun] worker disconnected before result was received\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }
            }
        }

        if (NowMs() - startTime > master->maxTimeMs) {
            fprintf(stderr, "[MasterRun] Deadline the time limit\n");
            status = 1;
            break;
        }
    }

    free(pollFds);

    if (status != 0) {
        CloseAllWorkers(master);
        return 1;
    }

    return 0;
}
