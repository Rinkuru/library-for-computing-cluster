#ifndef MASTER_MULTIPLEXING_H
#define MASTER_MULTIPLEXING_H

#include "master.h"

void ConfigureTcpFailureDetection(int socketFd, int timeoutMs);

//master
int MasterInitListenSocket(Master *master, const MasterConfig *config);

//worker
void ConfigureSocketTimeouts(int socketFd, int timeoutMs);

#endif
