#define _GNU_SOURCE

#include "worker.h"
#include "common_multiplexing.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    struct sigaction previousAction;
    int active;
} DeadlineTimer;

static volatile sig_atomic_t gDeadlineExpired = 0;

static void DeadlineSignalHandler(int signalNumber)
{
    (void)signalNumber;
    gDeadlineExpired = 1;
}

static void PacketPrepareEmpty(ClusterPacket *packet)
{
    packet->data = NULL;
    packet->size = 0U;
}

static void PacketDestroy(ClusterPacket *packet)
{
    if (packet == NULL) return;

    free(packet->data);
    PacketPrepareEmpty(packet);
}

static void WorkerPrepareEmpty(Worker *worker)
{
    worker->socketFd = -1;
    PacketPrepareEmpty(&worker->task);
    PacketPrepareEmpty(&worker->result);
    worker->resources.threads = 0;
    worker->resources.cores = 0;
    worker->resources.firstCore = 0;
    worker->maxTime = 0;
}

static void SleepMs(long milliseconds)
{
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000L;
    ts.tv_nsec = (milliseconds % 1000L) * 1000000L;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

static int DeadlineTimerStart(DeadlineTimer *timer, int timeoutMs)
{
    if (timer == NULL) return 1;

    memset(timer, 0, sizeof(*timer));
    gDeadlineExpired = 0;
    if (timeoutMs <= 0) return 0;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = DeadlineSignalHandler;
    sigemptyset(&action.sa_mask);

    if (sigaction(SIGALRM, &action, &timer->previousAction) == -1) {
        fprintf(stderr, "[DeadlineTimer] Unable to install SIGALRM handler\n");
        return 1;
    }

    struct itimerval timeout;
    memset(&timeout, 0, sizeof(timeout));
    timeout.it_value.tv_sec = timeoutMs / 1000;
    timeout.it_value.tv_usec = (suseconds_t)(timeoutMs % 1000) * 1000;

    if (setitimer(ITIMER_REAL, &timeout, NULL) == -1) {
        fprintf(stderr, "[DeadlineTimer] Unable to start timer\n");
        (void)sigaction(SIGALRM, &timer->previousAction, NULL);
        return 1;
    }

    timer->active = 1;
    return 0;
}

static void DeadlineTimerStop(DeadlineTimer *timer)
{
    if (timer == NULL || !timer->active) return;

    struct itimerval timeout;
    memset(&timeout, 0, sizeof(timeout));
    (void)setitimer(ITIMER_REAL, &timeout, NULL);
    (void)sigaction(SIGALRM, &timer->previousAction, NULL);
    timer->active = 0;
    gDeadlineExpired = 0;
}

static int DeadlineTimerExpired(void)
{
    return gDeadlineExpired != 0;
}

static int DeadlineExceeded(long startTime, int maxTimeMs, int *reported)
{
    int expired = DeadlineTimerExpired();
    if (!expired && maxTimeMs > 0) {
        long now = NowMs();
        expired = now >= 0L && now - startTime > maxTimeMs;
    }

    if (!expired) return 0;

    if (reported != NULL && *reported == 0) {
        fprintf(stderr, "[WorkerRun] Deadline the time limit\n");
        *reported = 1;
    }
    return 1;
}

static void BlockDeadlineSignalForMethod(void)
{
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGALRM);
    (void)pthread_sigmask(SIG_BLOCK, &signals, NULL);
}

static int WorkerCheckMasterAlive(int socketFd)
{
    struct pollfd pollFd;
    pollFd.fd = socketFd;
    pollFd.events = POLLIN | POLLRDHUP;
    pollFd.revents = 0;

    int pollResult;
    do {
        pollResult = poll(&pollFd, 1U, 0);
    } while (pollResult == -1 && errno == EINTR);

    if (pollResult == -1) {
        fprintf(stderr, "[WorkerIO] Unable to poll master socket\n");
        return 1;
    }
    if (pollResult == 0) {
        return 0;
    }
    if ((pollFd.revents & (POLLERR | POLLHUP | POLLNVAL | POLLRDHUP)) != 0) {
        fprintf(stderr, "[WorkerIO] Master socket failed\n");
        return 1;
    }
    if ((pollFd.revents & POLLIN) != 0) {
        char byte;
        ssize_t peeked = recv(socketFd, &byte, sizeof(byte), MSG_PEEK | MSG_DONTWAIT);
        if (peeked == 0) {
            fprintf(stderr, "[WorkerIO] Master closed connection\n");
            return 1;
        }
        if (peeked == -1 &&
            errno != EAGAIN &&
            errno != EWOULDBLOCK &&
            errno != EINTR) {
            fprintf(stderr, "[WorkerIO] Unable to inspect master socket\n");
            return 1;
        }
    }

    return 0;
}

static int WriteAll(int socketFd, const void *buffer, size_t size)
{
    const char *data = (const char *)buffer;
    size_t offset = 0U;
#ifdef MSG_NOSIGNAL
    int sendFlags = MSG_NOSIGNAL;
#else
    int sendFlags = 0;
#endif

    while (offset < size) {
        ssize_t written = send(socketFd, data + offset, size - offset, sendFlags);
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

static int ReadPacket(int socketFd, ClusterPacket *packet)
{
    unsigned char sizeBuffer[ClusterSizeHeaderSize];
    if (ReadAll(socketFd, sizeBuffer, sizeof(sizeBuffer)) != 0) {
        return 1;
    }

    uint64_t packetSize = ClusterDecodeSize(sizeBuffer);
    if (packetSize > (uint64_t)SIZE_MAX) {
        fprintf(stderr, "[WorkerIO] Packet size is too large\n");
        return 1;
    }

    packet->size = (size_t)packetSize;
    packet->data = NULL;
    if (packet->size == 0U) {
        return 0;
    }

    packet->data = malloc(packet->size);
    if (packet->data == NULL) {
        fprintf(stderr, "[WorkerIO] Unable to allocate packet data\n");
        PacketPrepareEmpty(packet);
        return 1;
    }

    if (ReadAll(socketFd, packet->data, packet->size) != 0) {
        PacketDestroy(packet);
        return 1;
    }

    return 0;
}

static int WritePacket(int socketFd, const ClusterPacket *packet)
{
    unsigned char sizeBuffer[ClusterSizeHeaderSize];
    ClusterEncodeSize((uint64_t)packet->size, sizeBuffer);

    if (WriteAll(socketFd, sizeBuffer, sizeof(sizeBuffer)) != 0) {
        return 1;
    }

    if (packet->size > 0U &&
        WriteAll(socketFd, packet->data, packet->size) != 0) {
        return 1;
    }

    return 0;
}

static void DestroyMethodResult(ClusterPacket *packet)
{
    if (packet == NULL) return;

    free(packet->data);
    free(packet);
}

static void DestroyThreadResults(WorkerThreadArgs *args, int count)
{
    if (args == NULL) return;

    for (int i = 0; i < count; ++i) {
        DestroyMethodResult(args[i].result);
        args[i].result = NULL;
    }
}

static void TimespecAfterMs(struct timespec *deadline, long milliseconds)
{
    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += milliseconds / 1000L;
    deadline->tv_nsec += (milliseconds % 1000L) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1L;
        deadline->tv_nsec -= 1000000000L;
    }
}

static int FindUnjoinedFinishedThread(WorkerThreadArgs *args, int count)
{
    for (int i = 0; i < count; ++i) {
        if (args[i].finished && !args[i].joined) {
            return i;
        }
    }
    return -1;
}

static int JoinFinishedThreads(pthread_t *tids, WorkerThreadArgs *args, int count, int *joinedThreads)
{
    for (;;) {
        int threadIndex = FindUnjoinedFinishedThread(args, count);

        if (threadIndex == -1) {
            return 0;
        }

        int ret = pthread_join(tids[threadIndex], NULL);
        if (ret != 0) {
            fprintf(stderr, "[WorkerRun] Unable to join thread\n");
            return 1;
        } else {
            *joinedThreads += 1;
        }
    }
}

static void *WorkerThreadFunc(void *threadArgs)
{
    WorkerThreadArgs *args = (WorkerThreadArgs *)threadArgs;
    if (args == NULL || args->method == NULL) {
        return NULL;
    }

    args->result = (ClusterPacket *)args->method(&args->task, sizeof(args->task));
    args->finished = true;

    return NULL;
}

static int AddSize(size_t *sum, size_t value)
{
    if (SIZE_MAX - *sum < value) {
        return 1;
    }

    *sum += value;
    return 0;
}

static int PackThreadResults(WorkerThreadArgs *args, int count, ClusterPacket *result)
{
    size_t packetSize = ClusterSizeHeaderSize;
    for (int i = 0; i < count; ++i) {
        if (args[i].result == NULL ||
            (args[i].result->data == NULL && args[i].result->size > 0U) ||
            AddSize(&packetSize, ClusterSizeHeaderSize) != 0 ||
            AddSize(&packetSize, args[i].result->size) != 0) {
            fprintf(stderr, "[WorkerRun] Method returned invalid packet\n");
            return 1;
        }
    }

    void *data = malloc(packetSize);
    if (data == NULL) {
        fprintf(stderr, "[WorkerRun] Unable to allocate result packet\n");
        return 1;
    }

    unsigned char *bytes = (unsigned char *)data;
    ClusterEncodeSize((uint64_t)count, bytes);
    size_t offset = ClusterSizeHeaderSize;

    for (int i = 0; i < count; ++i) {
        ClusterEncodeSize((uint64_t)args[i].result->size, bytes + offset);
        offset += ClusterSizeHeaderSize;
        if (args[i].result->size > 0U) {
            memcpy(bytes + offset, args[i].result->data, args[i].result->size);
            offset += args[i].result->size;
        }
    }

    PacketDestroy(result);
    result->data = data;
    result->size = packetSize;
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

static int ConnectToMaster(const struct addrinfo *address, int *socketFd, int maxTimeMs)
{
    long startTime = NowMs();

    while (true) {
        int status = TryConnectToMaster(address, socketFd);
        if (status == 0) return 0;
        if (status == 1) {
            fprintf(stderr, "[WorkerInit] Unable to connect to master\n");
            return 1;
        }

        if (maxTimeMs > 0 && NowMs() - startTime >= maxTimeMs) {
            fprintf(stderr, "[WorkerInit] Unable to connect to master before timeout\n");
            return 1;
        }

        long sleepMs = 500;
        if (maxTimeMs > 0) {
            long remaining = (long)maxTimeMs - (NowMs() - startTime);
            if (remaining <= 0L) {
                fprintf(stderr, "[WorkerInit] Unable to connect to master before timeout\n");
                return 1;
            }
            if (remaining < sleepMs) sleepMs = remaining;
        }

        printf("Wait for master to start\n");
        SleepMs(sleepMs);
    }
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
    ret = ConnectToMaster(addresses, &socketFd, config->max_time);
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
    ConfigureSocketTimeouts(worker->socketFd, config->max_time);
    ConfigureTcpFailureDetection(worker->socketFd, config->max_time);

    if (ReadPacket(worker->socketFd, &worker->task) != 0) {
        fprintf(stderr, "[WorkerInit] Unable to read task from master\n");
        freeaddrinfo(addresses);
        WorkerDestroy(worker);
        return 1;
    }

    freeaddrinfo(addresses);
    return 0;
}

int WorkerRun(Worker *worker, Method method)
{
    if (worker == NULL ||
        method == NULL ||
        worker->socketFd < 0 ||
        (worker->task.data == NULL && worker->task.size > 0U) ||
        worker->resources.threads <= 0 ||
        worker->resources.cores <= 0 ||
        worker->resources.firstCore < 0 ||
        worker->resources.firstCore + worker->resources.cores > CPU_SETSIZE) {
        fprintf(stderr, "[WorkerRun] NULL config\n");
        return 1;
    }

    pthread_t *tids = calloc((size_t)worker->resources.threads, sizeof(*tids));
    WorkerThreadArgs *args = calloc((size_t)worker->resources.threads, sizeof(*args));
    if (tids == NULL || args == NULL) {
        fprintf(stderr, "[WorkerRun] Unable to allocate thread resources\n");
        free(tids);
        free(args);
        return 1;
    }

    long startTime = NowMs();
    int status = 0;
    int createdThreads = 0;
    int joinedThreads = 0;

    for (int i = 0; i < worker->resources.threads; ++i) {
        if (WorkerCheckMasterAlive(worker->socketFd) != 0) {
            status = 1;
            break;
        }

        args[i].method = method;
        args[i].task.threadIndex = (size_t)i;
        args[i].task.threadsCount = (size_t)worker->resources.threads;
        args[i].task.data = worker->task.data;
        args[i].task.size = worker->task.size;
        args[i].result = NULL;
        args[i].finished = false;
        args[i].joined = false;

        pthread_attr_t threadAttributes;
        int ret = pthread_attr_init(&threadAttributes);
        if (ret != 0) {
            fprintf(stderr, "[WorkerRun] Unable to initialize thread attributes\n");
            status = 1;
            break;
        }

        cpu_set_t assignedHarts;
        CPU_ZERO(&assignedHarts);
        CPU_SET((size_t)(worker->resources.firstCore + i % worker->resources.cores),
                &assignedHarts);

        ret = pthread_attr_setaffinity_np(&threadAttributes,
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

        createdThreads += 1;
        if (NowMs() - startTime > worker->maxTime) {
            fprintf(stderr, "[WorkerRun] Deadline the time limit\n");
            status = 1;
            break;
        }
    }

    while (joinedThreads < createdThreads) {
        while (FindUnjoinedFinishedThread(args, createdThreads) == -1) {
            struct timespec deadline;
            TimespecAfterMs(&deadline, 100L);
            break;
        }

        if (JoinFinishedThreads(tids, args, createdThreads, &joinedThreads) != 0) {
            status = 1;
        }
        if (WorkerCheckMasterAlive(worker->socketFd) != 0) {
            status = 1;
        }
        if (NowMs() - startTime > worker->maxTime) {
            fprintf(stderr, "[WorkerRun] Deadline the time limit\n");
            status = 1;
        }
    }

    if (status == 0 && PackThreadResults(args, worker->resources.threads, &worker->result) != 0) {
        status = 1;
    }

    DestroyThreadResults(args, worker->resources.threads);
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

    if (WorkerCheckMasterAlive(worker->socketFd) != 0) {
        fprintf(stderr, "[WorkerSendResult] Master is unavailable\n");
        return 1;
    }

    if (WritePacket(worker->socketFd, &worker->result) != 0) {
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

    PacketDestroy(&worker->task);
    PacketDestroy(&worker->result);
    WorkerPrepareEmpty(worker);
}
