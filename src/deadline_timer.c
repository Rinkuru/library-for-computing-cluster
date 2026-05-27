#define _GNU_SOURCE

#include "deadline_timer.h"

#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static volatile sig_atomic_t deadlineExpired = 0;

static void DeadlineSignalHandler(int signalNumber) {
    (void)signalNumber;
    deadlineExpired = 1;
}

int DeadlineTimerStart(DeadlineTimer *timer, int timeoutMs) {
    if (timer == NULL) return 1;

    memset(timer, 0, sizeof(*timer));
    deadlineExpired = 0;
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

void DeadlineTimerStop(DeadlineTimer *timer) {
    if (timer == NULL || !timer->active) return;

    struct itimerval timeout;
    memset(&timeout, 0, sizeof(timeout));
    (void)setitimer(ITIMER_REAL, &timeout, NULL);
    (void)sigaction(SIGALRM, &timer->previousAction, NULL);
    timer->active = 0;
    deadlineExpired = 0;
}

int DeadlineTimerExpired(void) {
    return deadlineExpired != 0;
}

int DeadlineExceeded(long startTime, int maxTimeMs, int *reported) {
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

void BlockDeadlineSignalForMethod(void) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGALRM);
    (void)pthread_sigmask(SIG_BLOCK, &signals, NULL);
}
