#define _GNU_SOURCE

#include "common_multiplexing.h"

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
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int MasterInitListenSocket(Master *master, const MasterConfig *config) {
    master->listenSocketFd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    if (master->listenSocketFd == -1) {
        fprintf(stderr, "[MasterInit] Unable to create socket!\n");
        MasterDestroy(master);
        return 1;
    }

    int yes = 1;
    if (setsockopt(master->listenSocketFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        fprintf(stderr, "[MasterInit] Unable to set SO_REUSEADDR socket option\n");
        MasterDestroy(master);
        return 1;
    }

    struct sockaddr_in listenAddr;
    listenAddr.sin_family = AF_INET;
    listenAddr.sin_port = htons((uint16_t)config->port);
    listenAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (config->host != NULL) {
        if (inet_pton(AF_INET, config->host, &listenAddr.sin_addr) != 1) {  // Парсит строковый IP
            MasterDestroy(master);
            return 1;
        }
    }

    if (bind(master->listenSocketFd, (struct sockaddr *)&listenAddr, sizeof(listenAddr)) == -1) {
        fprintf(stderr, "[MasterInit] Unable to bind\n");
        MasterDestroy(master);
        return 1;
    }

    if (listen(master->listenSocketFd, config->required_workers) == -1) {
        fprintf(stderr, "[MasterInit] Unable to listen() on a socket\n");
        MasterDestroy(master);
        return 1;
    }
    return 0;
}

void ConfigureTcpFailureDetection(int socketFd, int timeoutMs) {
    int yes = 1;
    (void)setsockopt(socketFd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

    if (timeoutMs > 0) {
        (void)setsockopt(socketFd, IPPROTO_TCP, TCP_USER_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    }

    int keepIdle = 1;
    (void)setsockopt(socketFd, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(keepIdle));

    int keepInterval = 1;
    (void)setsockopt(socketFd, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(keepInterval));

    int keepCount = 3;
    (void)setsockopt(socketFd, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(keepCount));
}

static int SocketTimeoutMs(int maxTimeMs) {
    if (maxTimeMs <= 0) return 1000;
    if (maxTimeMs < 1000) return maxTimeMs;
    return 1000;
}

static void FillTimevalMs(struct timeval *tv, int timeoutMs) {
    tv->tv_sec = timeoutMs / 1000;
    tv->tv_usec = (suseconds_t)(timeoutMs % 1000) * 1000;
}

void ConfigureSocketTimeouts(int socketFd, int timeoutMs) {
    struct timeval timeout;
    FillTimevalMs(&timeout, SocketTimeoutMs(timeoutMs));
    (void)setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    (void)setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}
