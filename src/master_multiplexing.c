#define _GNU_SOURCE

#include "master_multiplexing.h"

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
