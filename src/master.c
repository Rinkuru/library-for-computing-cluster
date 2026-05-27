#define _GNU_SOURCE

#include "common.h"
#include "master.h"
#include "common_multiplexing.h"
#include "deadline_timer.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
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
    worker->taskOffset = 0U;
    worker->taskSize = 0U;
    worker->resultSize = 0U;
    memset(worker->sizeBuffer, 0, sizeof(worker->sizeBuffer));
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

static short SocketPeerClosedEvents(void) {
    return POLLHUP | POLLRDHUP;
}

static int PollTimeoutMs(long startTime, int maxTimeMs)
{
    if (maxTimeMs <= 0) return -1;

    long elapsed = NowMs() - startTime;
    long remaining = (long)maxTimeMs - elapsed;
    if (remaining <= 0L) return 0;
    if (remaining > (long)INT_MAX) return INT_MAX;
    return (int)remaining;
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
        shutdown(worker->socketFd, SHUT_RDWR);
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

static void FreeResults(ClusterPacket *results, int count)
{
    if (results == NULL) return;

    for (int i = 0; i < count; ++i) {
        free(results[i].data);
        results[i].data = NULL;
        results[i].size = 0U;
    }
}

static int MasterAcceptWorker(Master *master, size_t taskDataSize)
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
    ConfigureTcpFailureDetection(workerSocket, master->maxTimeMs);

    if (SetNonBlocking(workerSocket) != 0) {  // переводит в неблокирующий режим
        fprintf(stderr, "[MasterRun] Unable to set worker socket nonblocking\n");
        close(workerSocket);
        return 1;
    }

    int workerIndex = master->workersCount;
    MasterWorker *worker = &master->workers[workerIndex];
    worker->socketFd = workerSocket;
    worker->state = SendTaskSize;
    worker->transferOffset = 0U;
    worker->taskOffset = SplitPoint(taskDataSize, workerIndex, master->workersCapacity);
    size_t taskEnd = SplitPoint(taskDataSize, workerIndex + 1, master->workersCapacity);
    worker->taskSize = taskEnd - worker->taskOffset;
    worker->resultSize = 0U;
    ClusterEncodeSize((uint64_t)worker->taskSize, worker->sizeBuffer);

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
        case SendTaskSize:
        case SendTaskData:
            pollFd->fd = master->workers[i].socketFd;
            pollFd->events = POLLOUT | POLLRDHUP;
            break;
        case ReadResultSize:
        case ReadResultData:
            pollFd->fd = master->workers[i].socketFd;
            pollFd->events = POLLIN | POLLRDHUP;
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

    if (config == NULL ||
        config->port <= 0 ||
        config->required_workers <= 0) {
        fprintf(stderr, "[MasterInit] NULL config\n");
        return 1;
    }

    if (MasterPrepare(master, config)) return 1;

    if (MasterInitListenSocket(master, config)) return 1;

    return 0;
}

int MasterRun(Master *master, const void *data, size_t size, ClusterPacket *results)
{
    if (master == NULL ||
        (data == NULL && size > 0U) ||
        results == NULL ||
        master->listenSocketFd < 0 ||
        master->workersCapacity <= 0) {
        fprintf(stderr, "[MasterRun] NULL config\n");
        return 1;
    }

    for (int i = 0; i < master->workersCapacity; ++i) {
        results[i].data = NULL;
        results[i].size = 0U;
    }

    struct pollfd *pollFds = calloc((size_t)master->workersCapacity + 1U, sizeof(struct pollfd));
    if (pollFds == NULL) {
        fprintf(stderr, "[MasterRun] Unable to calloc\n");
        return 1;
    }

    DeadlineTimer deadlineTimer;
    if (DeadlineTimerStart(&deadlineTimer, master->maxTimeMs) != 0) {
        free(pollFds);
        return 1;
    }

    int doneWorkers = 0;
    int status = 0;

    long startTime = NowMs();
    while (doneWorkers < master->workersCapacity) {
        if (DeadlineTimerExpired()) {
            fprintf(stderr, "[MasterRun] Deadline the time limit\n");
            status = 1;
            break;
        }

        PreparePollFds(master, pollFds);

        int timeoutMs = PollTimeoutMs(startTime, master->maxTimeMs);
        if (timeoutMs == 0) {
            fprintf(stderr, "[MasterRun] Deadline the time limit\n");
            status = 1;
            break;
        }

        int pollResult = poll(pollFds, (nfds_t)master->workersCapacity + 1U, timeoutMs);
        if (pollResult == -1) {
            if (errno == EINTR) {
                if (DeadlineTimerExpired()) {
                    fprintf(stderr, "[MasterRun] Deadline the time limit\n");
                    status = 1;
                    break;
                }
                continue;
            }
            fprintf(stderr, "[MasterRun] Unable to poll-wait for data on descriptors\n");
            status = 1;
            break;
        }
        if (pollResult == 0) {
            fprintf(stderr, "[MasterRun] Deadline the time limit\n");
            status = 1;
            break;
        }

        if ((pollFds[0].revents & (POLLERR | POLLHUP | POLLNVAL | POLLRDHUP)) != 0) {
            fprintf(stderr, "[MasterRun] listen socket failed\n");
            status = 1;
            break;
        }

        printf("I'm waiting worker\n");
        if ((pollFds[0].revents & POLLIN) != 0) {
            if (MasterAcceptWorker(master, size) != 0) {
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

            if (worker->state == SendTaskSize) {
                if ((revents & POLLOUT) != 0) {
                    TransferStatus transferStatus = WritePart(worker->socketFd,
                                                              worker->sizeBuffer,
                                                              ClusterSizeHeaderSize,
                                                              &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to send task size to worker\n");
                        worker->state = Failed;
                        status = 1;
                        break;
                    }

                    if (transferStatus == TransferComplete) {
                        worker->transferOffset = 0U;
                        worker->state = SendTaskData;
                    }
                }

                if ((revents & SocketPeerClosedEvents()) != 0 && worker->state == SendTaskSize) {
                    fprintf(stderr, "[MasterRun] worker disconnected before task was sent\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }

                continue;
            }

            if (worker->state == SendTaskData) {
                if ((revents & POLLOUT) != 0) {
                    const char *bytes = (const char *)data;
                    const void *taskData = worker->taskSize == 0U ? NULL : bytes + worker->taskOffset;
                    TransferStatus transferStatus = WritePart(worker->socketFd,
                                                              taskData,
                                                              worker->taskSize,
                                                              &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to send task data to worker\n");
                        worker->state = Failed;
                        status = 1;
                        break;
                    }

                    if (transferStatus == TransferComplete) {
                        worker->transferOffset = 0U;
                        worker->state = ReadResultSize;
                    }
                }

                if ((revents & SocketPeerClosedEvents()) != 0 && worker->state == SendTaskData) {
                    fprintf(stderr, "[MasterRun] worker disconnected before task was sent\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }

                continue;
            }

            if (worker->state == ReadResultSize) {
                if ((revents & POLLIN) != 0) {
                    TransferStatus transferStatus = ReadPart(worker->socketFd,
                                                             worker->sizeBuffer,
                                                             ClusterSizeHeaderSize,
                                                             &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to read result size from worker\n");
                        worker->state = Failed;
                        status = 1;
                        break;
                    }

                    if (transferStatus == TransferComplete) {
                        uint64_t resultSize = ClusterDecodeSize(worker->sizeBuffer);
                        if (resultSize > (uint64_t)SIZE_MAX) {
                            fprintf(stderr, "[MasterRun] result size is too large\n");
                            worker->state = Failed;
                            status = 1;
                            break;
                        }

                        results[i].size = (size_t)resultSize;
                        results[i].data = NULL;
                        worker->resultSize = results[i].size;
                        if (worker->resultSize > 0U) {
                            results[i].data = malloc(worker->resultSize);
                            if (results[i].data == NULL) {
                                fprintf(stderr, "[MasterRun] unable to allocate result data\n");
                                worker->state = Failed;
                                status = 1;
                                break;
                            }
                        }

                        worker->transferOffset = 0U;
                        if (worker->resultSize == 0U) {
                            worker->state = Done;
                            doneWorkers += 1;
                        } else {
                            worker->state = ReadResultData;
                        }
                    }
                }

                if ((revents & SocketPeerClosedEvents()) != 0 && worker->state == ReadResultSize) {
                    fprintf(stderr, "[MasterRun] worker disconnected before result was received\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }

                continue;
            }

            if (worker->state == ReadResultData) {
                if ((revents & POLLIN) != 0) {
                    TransferStatus transferStatus = ReadPart(worker->socketFd,
                                                             results[i].data,
                                                             worker->resultSize,
                                                             &worker->transferOffset);

                    if (transferStatus == TransferFailed) {
                        fprintf(stderr, "[MasterRun] unable to read result data from worker\n");
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

                if ((revents & SocketPeerClosedEvents()) != 0 && worker->state == ReadResultData) {
                    fprintf(stderr, "[MasterRun] worker disconnected before result was received\n");
                    worker->state = Failed;
                    status = 1;
                    break;
                }
            }
        }

        if (status != 0) {
            break;
        }

        if (NowMs() - startTime > master->maxTimeMs) {
            fprintf(stderr, "[MasterRun] Deadline the time limit\n");
            status = 1;
            break;
        }
    }

    DeadlineTimerStop(&deadlineTimer);
    free(pollFds);

    if (status != 0) {
        CloseAllWorkers(master);
        FreeResults(results, master->workersCapacity);
        return 1;
    }

    return 0;
}
