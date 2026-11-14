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

/*
 * Policy modules provide strong definitions for these hooks. The weak
 * defaults in scheduler.c allow the coordinator to build before those modules
 * are linked.
 *
 * processes is the linked list of all PCBs. processArrived is called in
 * arrival-time order, then process-ID order for ties. selectNextProcess peeks
 * at the next READY PCB without removing it; processDispatched is called only
 * after its context switch finishes and it actually starts or resumes.
 * shouldPreempt runs after same-tick arrivals; quantum_used is CPU time since
 * last dispatch. RR should preempt at quantum expiry only when another process
 * is READY. Same-tick arrivals are queued before the expired process, while
 * arrivals during its context switch follow that process in the ready queue.
 * processPreempted runs after the coordinator stops the process,
 * so RR can append it behind arrivals already queued for that tick.
 * processUnblocked is called by the coordinator after unblockProcess makes a
 * blocked PCB READY, allowing policy-owned ready queues to add it.
 *
 * handleResourceEvents runs before completion checks on every scheduler pass.
 * It must process releases before captures for the running process; the
 * coordinator handles completion immediately afterward. Resource event times
 * are offsets in process CPU execution time. Hooks must not send process
 * signals or create child processes.
 */
void processArrived(PCB *process, SchedulingAlgorithm algorithm);
PCB *selectNextProcess(PCB *processes, SchedulingAlgorithm algorithm, int now, int quantum);
void processDispatched(PCB *process, SchedulingAlgorithm algorithm);
int shouldPreempt(PCB *running, PCB *processes, SchedulingAlgorithm algorithm, int now, int quantum, int quantum_used);
void processPreempted(PCB *process, SchedulingAlgorithm algorithm);
void processUnblocked(PCB *process, SchedulingAlgorithm algorithm);
void handleResourceEvents(PCB *processes, SchedulerResourceEvent *events, int now, SchedulingAlgorithm algorithm);

#endif




