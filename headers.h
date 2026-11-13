#ifndef HEADERS_H
#define HEADERS_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

/* Keep the clock key supplied with the project. */
#define SHKEY 300

#define MAX_PROCESSES 1000
#define MAX_RESOURCES 1000
#define RESOURCE_NAME_LEN 32

#define MSG_PROCESS   2L
#define MSG_RESOURCE  3L
#define MSG_INPUT_END 4L
#define MSG_FINISHED  5L

#define ALG_SJF 1
#define ALG_RR  2
#define ALG_HPF 3

typedef short bool;
#define true 1
#define false 0

/* Shared clock address. Each executable has its own copy of this pointer. */
int *shmaddr = NULL;

static inline int getClk(void)
{
    return *shmaddr;
}

static inline void initClk(void)
{
    int shmid = shmget(SHKEY, sizeof(int), 0444);
    while (shmid == -1)
    {
        if (errno != ENOENT && errno != EINVAL)
            perror("shmget clock");
        sleep(1);
        shmid = shmget(SHKEY, sizeof(int), 0444);
    }

    shmaddr = (int *)shmat(shmid, NULL, 0);
    if (shmaddr == (void *)-1)
    {
        perror("shmat clock");
        exit(EXIT_FAILURE);
    }
}

static inline void destroyClk(bool terminateAll)
{
    if (shmaddr != NULL && shmaddr != (void *)-1)
    {
        shmdt(shmaddr);
        shmaddr = NULL;
    }

    /* Prefer that the generator (the owner) terminates clk.out by PID. */
    if (terminateAll)
        killpg(getpgrp(), SIGINT);
}

/* One common message layout makes the queue interface easy to share. */
typedef struct
{
    long mtype;                 /* Must be first for System V message queues. */
    int id;
    int arrival;
    int runtime;
    int priority;
    int captureTime;
    int releaseTime;
    char resource[RESOURCE_NAME_LEN];
} IPCMessage;

#define IPC_MESSAGE_SIZE (sizeof(IPCMessage) - sizeof(long))

#endif
