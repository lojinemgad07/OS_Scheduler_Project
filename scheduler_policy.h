#ifndef SCHEDULER_POLICY_H
#define SCHEDULER_POLICY_H

#include "process.h"

typedef enum {
    SCHED_SJF = 1,
    SCHED_RR = 2,
    SCHED_HPF = 3
} SchedulingAlgorithm;

typedef struct SchedulerResourceEvent {
    ResourceEvent event;
    int capture_handled;
    int release_handled;
    struct SchedulerResourceEvent *next;
} SchedulerResourceEvent;

/* Implement these policy hooks in the algorithm modules.
 * selectNextProcess returns a READY PCB, or NULL if there is no candidate.
 * shouldPreempt may request a preemption of the currently RUNNING PCB.
 * Resource events are exposed to the resource manager on each scheduler pass;
 * their capture/release times are offsets in process CPU execution time.
 * Policy code must not start/stop child processes directly. */
PCB *selectNextProcess(PCB *processes, SchedulingAlgorithm algorithm,
                       int now, int quantum);
int shouldPreempt(PCB *running, PCB *processes,
                  SchedulingAlgorithm algorithm, int now,
                  int quantum, int quantum_used);
void handleResourceEvents(PCB *processes, SchedulerResourceEvent *events,
                          int now);

#endif
