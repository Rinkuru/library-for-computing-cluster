#ifndef DEADLINE_TIMER_H
#define DEADLINE_TIMER_H

#include <signal.h>

typedef struct {
    struct sigaction previousAction;
    int active;
} DeadlineTimer;

int DeadlineTimerStart(DeadlineTimer *timer, int timeoutMs);

void DeadlineTimerStop(DeadlineTimer *timer);

int DeadlineTimerExpired(void);

int DeadlineExceeded(long startTime, int maxTimeMs, int *reported);

void BlockDeadlineSignalForMethod(void);

#endif
