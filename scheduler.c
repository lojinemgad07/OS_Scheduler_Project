#include "headers.h"
#include "process.h"
#include "process_control.h"
#include "ipc.h"
#include "scheduler_policy.h"

#include <errno.h>
#include <limits.h>
#include <sys/msg.h>
#include <sys/wait.h>

/* Weak defaults keep the coordinator buildable before policy files are merged. */
__attribute__((weak)) void processArrived(PCB *process, SchedulingAlgorithm algorithm)
{
    (void)process;
    (void)algorithm;
}

__attribute__((weak)) PCB *selectNextProcess(PCB *processes, SchedulingAlgorithm algorithm, int now, int quantum)
{
    (void)processes;
    (void)algorithm;
    (void)now;
    (void)quantum;
    return NULL;
}

__attribute__((weak)) void processDispatched(PCB *process, SchedulingAlgorithm algorithm)
{
    (void)process;
    (void)algorithm;
}

__attribute__((weak)) int shouldPreempt(PCB *running, PCB *processes, SchedulingAlgorithm algorithm, int now, int quantum, int quantum_used)
{
    (void)running;
    (void)processes;
    (void)algorithm;
    (void)now;
    (void)quantum;
    (void)quantum_used;
    return 0;
}

__attribute__((weak)) void processPreempted(PCB *process, SchedulingAlgorithm algorithm)
{
    (void)process;
    (void)algorithm;
}

__attribute__((weak)) void processUnblocked(PCB *process, SchedulingAlgorithm algorithm)
{
    (void)process;
    (void)algorithm;
}

__attribute__((weak)) void handleResourceEvents(PCB *processes, SchedulerResourceEvent *events, int now, SchedulingAlgorithm algorithm)
{
    (void)processes;
    (void)events;
    (void)now;
    (void)algorithm;
}

static volatile sig_atomic_t stop_requested;

static void requestStop(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static PCB *findProcess(PCB *head, int id)
{
    for (PCB *p = head; p; p = p->next) {
        if (p->id == id) {
            return p;
        }
    }

    return NULL;
}

static int receiveMessages(int qid, PCB **head, PCB **tail, SchedulerResourceEvent **resource_head, int *input_ended, int *arrival_watermark, SchedulingAlgorithm algorithm)
{
    SchedulerMessage m;

    for (;;) {
        ssize_t n = msgrcv(qid, &m, sizeof(m) - sizeof(m.type), 0, IPC_NOWAIT);
        if (n == -1) {
            if (errno == ENOMSG) {
                return 0;
            }
            if (errno == EINTR) {
                if (stop_requested) {
                    return 0;
                }
                continue;
            }

            perror("scheduler msgrcv");
            return -1;
        }

        if (m.type == MSG_PROCESS) {
            if (findProcess(*head, m.id)) {
                fprintf(stderr,
                        "scheduler: duplicate process id %d ignored\n",
                        m.id);
                continue;
            }

            PCB *p = calloc(1, sizeof(*p));
            if (!p) {
                perror("calloc PCB");
                return -1;
            }

            p->id = m.id;
            p->arrival_time = m.arrival;
            p->runtime = m.runtime;
            p->remaining_time = m.runtime;
            p->original_priority = m.priority;
            p->effective_priority = m.priority;
            p->first_start_time = -1;
            p->last_start_time = -1;
            p->finish_time = -1;
            p->pid = 0;
            p->state = NEW;

            if (*tail) {
                (*tail)->next = p;
            } else {
                *head = p;
            }
            *tail = p;
        } else if (m.type == MSG_RESOURCES) {
            SchedulerResourceEvent *node = calloc(1, sizeof(*node));
            if (!node) {
                perror("calloc resource event");
                return -1;
            }

            node->event.pid = m.id;
            snprintf(node->event.resource_id, sizeof(node->event.resource_id), "%s", m.resource_id);
            node->event.capture_time = m.capture_time;
            node->event.release_time = m.release_time;
            node->next = *resource_head;
            *resource_head = node;
            /* The HPF/resource module consumes these retained events. */
        } else if (m.type == MSG_TIME) {
            if (m.arrival > *arrival_watermark) {
                *arrival_watermark = m.arrival;
            }
        } else if (m.type == MSG_END) {
            *input_ended = 1;
        }
    }
}

static void accountElapsed(PCB *head, PCB *running, int *last_time, int now, int *quantum_used)
{
    int elapsed = now - *last_time;
    if (elapsed <= 0) {
        return;
    }

    for (PCB *p = head; p; p = p->next) {
        if (p == running && p->state == RUNNING) {
            int used = elapsed < p->remaining_time ? elapsed : p->remaining_time;
            p->executed_time += used;
            p->remaining_time -= used;
            *quantum_used += used;
        } else if (p->state == READY) {
            p->waiting_time += elapsed;
        }
    }

    *last_time = now;
}

static int reapFinished(PCB *head, PCB **running, int now)
{
    for (PCB *p = head; p; p = p->next) {
        if (p->pid <= 0 || p->state == FINISHED) {
            continue;
        }

        int status;
        pid_t result = waitpid(p->pid, &status, WNOHANG);
        if (result == p->pid) {
            if (finishProcess(p) == -1) {
                perror("finishProcess");
                return -1;
            }
            p->remaining_time = 0;
            p->finish_time = now;
            if (*running == p) {
                *running = NULL;
            }
        } else if (result == -1 && errno != EINTR && errno != ECHILD) {
            perror("scheduler waitpid");
            return -1;
        }

        if (p->state != FINISHED && p->remaining_time <= 0) {
            if (finishProcess(p) == -1) {
                perror("finishProcess");
                return -1;
            }
            p->finish_time = now;
            if (*running == p) {
                *running = NULL;
            }
        }
    }

    return 0;
}

static int hasReadyProcess(PCB *head, PCB *running)
{
    for (PCB *p = head; p; p = p->next) {
        if (p != running && p->state == READY) {
            return 1;
        }
    }
    return 0;
}
static int allFinished(PCB *head)
{
    if (!head) {
        return 0;
    }

    for (PCB *p = head; p; p = p->next) {
        if (p->state != FINISHED) {
            return 0;
        }
    }

    return 1;
}

static void terminateChildren(PCB *head)
{
    for (PCB *p = head; p; p = p->next) {
        if (p->pid > 0) {
            kill(p->pid, SIGTERM);
            while (waitpid(p->pid, NULL, 0) == -1 && errno == EINTR) {
            }
            p->pid = 0;
        }
    }
}

static void freeData(PCB *head, SchedulerResourceEvent *resources)
{
    while (head) {
        PCB *next = head->next;
        free(head);
        head = next;
    }

    while (resources) {
        SchedulerResourceEvent *next = resources->next;
        free(resources);
        resources = next;
    }
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr,
                "usage: scheduler algorithm quantum context_switch_time\n");
        return 2;
    }

    char *end;
    long algorithm_arg = strtol(argv[1], &end, 10);
    if (*end || algorithm_arg < SCHED_SJF || algorithm_arg > SCHED_HPF) {
        fprintf(stderr, "scheduler: invalid algorithm\n");
        return 2;
    }
    SchedulingAlgorithm algorithm = (SchedulingAlgorithm)algorithm_arg;

    long quantum_arg = strtol(argv[2], &end, 10);
    if (*end || quantum_arg < 0 || quantum_arg > INT_MAX ||
        (algorithm == SCHED_RR && quantum_arg == 0)) {
        fprintf(stderr, "scheduler: invalid quantum\n");
        return 2;
    }
    int quantum = (int)quantum_arg;

    long context_arg = strtol(argv[3], &end, 10);
    if (*end || context_arg < 0 || context_arg > INT_MAX) {
        fprintf(stderr, "scheduler: invalid context-switch time\n");
        return 2;
    }
    int context_switch = (int)context_arg;

    struct sigaction sa;
    sa.sa_handler = requestStop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    int qid = msgget(SCHEDULER_QUEUE_KEY, 0666);
    if (qid == -1) {
        perror("scheduler msgget");
        return 1;
    }
    initClk();

    PCB *processes = NULL;
    PCB *tail = NULL;
    PCB *running = NULL;
    SchedulerResourceEvent *resources = NULL;
    int input_ended = 0;
    int arrival_watermark = -1;
    int quantum_used = 0;
    int last_time = getClk();
    int switching_until = -1;
    int switch_pending = 0;
    int warned_no_policy = 0;

    while (!stop_requested) {
        int now = getClk();
        accountElapsed(processes, running, &last_time, now, &quantum_used);

        if (receiveMessages(qid,
                            &processes,
                            &tail,
                            &resources,
                            &input_ended,
                            &arrival_watermark,
                            algorithm) == -1) {
            break;
        }

        /* Running-process resource events precede completion at this tick. */
        handleResourceEvents(processes, resources, now, algorithm);
        for (PCB *p = processes; p; p = p->next) {
            if (p->needs_ready_queue_update) {
                processUnblocked(p, algorithm);
                p->needs_ready_queue_update = 0;
            }
        }
        if (running && running->state != RUNNING) {
            running = NULL;
            quantum_used = 0;
            switch_pending = 1;
        }

        PCB *running_before_reap = running;
        if (reapFinished(processes, &running, now) == -1) {
            break;
        }
        if (running_before_reap && !running) {
            quantum_used = 0;
            switch_pending = 1;
        }

        /* Admit arrivals after running-process events and finishes. */
        for (PCB *p = processes; p; p = p->next) {
            if (!p->policy_registered && p->arrival_time <= now) {
                p->waiting_time += now - p->arrival_time;
                p->state = READY;
                processArrived(p, algorithm);
                p->policy_registered = 1;
            }
        }

        /* Wait for the generator's same-tick arrival batch before RR decisions. */
        if (!input_ended && arrival_watermark < now) {
            usleep(1000);
            continue;
        }

        if (running) {
            int rr_quantum_expired = algorithm == SCHED_RR &&
                                     quantum_used >= quantum;
            int rr_queue_empty = rr_quantum_expired &&
                                 !hasReadyProcess(processes, running);

            if (!rr_queue_empty &&
                shouldPreempt(running,
                              processes,
                              algorithm,
                              now,
                              quantum,
                              quantum_used)) {
                PCB *stopped = running;
                if (stopProcess(stopped) == -1) {
                    perror("stopProcess");
                    break;
                }
                processPreempted(stopped, algorithm);
                running = NULL;
                quantum_used = 0;
                switch_pending = 0;
                switching_until = context_switch ? now + context_switch : now;
            } else if (rr_quantum_expired) {
                /* Continue with another quantum; no stop or context switch occurs. */
                quantum_used %= quantum;
            }
        }

        if (!running && (switching_until < 0 || now >= switching_until)) {
            PCB *next = selectNextProcess(processes, algorithm, now, quantum);
            if (next) {
                if (next->state != READY || next->remaining_time <= 0) {
                    fprintf(stderr,
                            "scheduler policy returned a non-ready process\n");
                    break;
                }

                if (switch_pending && context_switch > 0 && switching_until < 0) {
                    switching_until = now + context_switch;
                    switch_pending = 0;
                } else {
                    if (next->pid == 0) {
                        if (startProcess(next) == -1) {
                            perror("startProcess");
                            break;
                        }
                        if (next->first_start_time < 0) {
                            next->first_start_time = now;
                        }
                    } else if (resumeProcess(next) == -1) {
                        perror("resumeProcess");
                        break;
                    }

                    next->last_start_time = now;
                    next->state = RUNNING;
                    processDispatched(next, algorithm);
                    running = next;
                    quantum_used = 0;
                    switch_pending = 0;
                    switching_until = -1;
                    last_time = now;
                }
            } else {
                if (switching_until >= 0 && now >= switching_until) {
                    switching_until = -1;
                }
                if (switching_until < 0) {
                    switch_pending = 0;
                }

                if (input_ended && allFinished(processes)) {
                    break;
                }
                if (input_ended && !warned_no_policy) {
                    fprintf(stderr,
                            "scheduler: no scheduling policy selected a READY "
                            "process; link the SJF/RR/HPF policy hooks\n");
                    warned_no_policy = 1;
                    break;
                }
            }
        }

        usleep(1000);
    }

    terminateChildren(processes);
    destroyClk(false);
    freeData(processes, resources);
    return stop_requested ? 130 : 0;
}















