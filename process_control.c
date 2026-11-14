#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include "process.h"
#include <errno.h>
#include <string.h>

static int signalProcess(PCB *p, int sig)
{
    if (!p || p->pid <= 0) { errno = EINVAL; return -1; }
    if (kill(p->pid, sig) == -1) return -1;
    return 0;
}

int startProcess(PCB *p)
{
    if (!p || p->runtime <= 0) { errno = EINVAL; return -1; }
    pid_t child = fork();
    if (child == -1) return -1;
    if (child == 0) {
        char runtime[32];
        snprintf(runtime, sizeof(runtime), "%d", p->remaining_time > 0 ? p->remaining_time : p->runtime);
        execl("./process.out", "process.out", runtime, (char *)NULL);
        perror("exec process.out");
        _exit(127);
    }
    p->pid = child;
    p->state = RUNNING;
    return 0;
}

int stopProcess(PCB *p)
{
    if (signalProcess(p, SIGSTOP) == -1) return -1;
    p->state = READY;
    return 0;
}

int resumeProcess(PCB *p)
{
    if (signalProcess(p, SIGCONT) == -1) return -1;
    p->state = RUNNING;
    return 0;
}

int blockProcess(PCB *p)
{
    if (!p) { errno = EINVAL; return -1; }
    if (p->pid > 0 && p->state == RUNNING && signalProcess(p, SIGSTOP) == -1) return -1;
    p->state = BLOCKED;
    return 0;
}

int unblockProcess(PCB *p)
{
    if (!p || p->state != BLOCKED) { errno = EINVAL; return -1; }
    p->state = READY;
    return 0;
}

int finishProcess(PCB *p)
{
    if (!p) { errno = EINVAL; return -1; }
    if (p->pid > 0) {
        int status;
        pid_t r;
        do { r = waitpid(p->pid, &status, 0); } while (r == -1 && errno == EINTR);
        if (r == -1 && errno != ECHILD) return -1;
        p->pid = 0;
    }
    p->state = FINISHED;
    return 0;
}


