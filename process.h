#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

/* Possible states of a process */
typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    FINISHED
} ProcessState;

/* Information read from processes.txt */
typedef struct {
    int id;
    int arrival;
    int runtime;
    int priority;
} Process;

/* Process Control Block */
typedef struct PCB {

    /* Basic information */
    int id;
    int arrival_time;
    int runtime;

    /* Execution tracking */
    int remaining_time;
    int executed_time;

    /* Scheduling information */
    int waiting_time;
    int original_priority;
    int effective_priority;

    /* Timing information */
    int first_start_time;
    int last_start_time;
    int finish_time;

    /* Linux process ID */
    pid_t pid;

    /* Current state */
    ProcessState state;

    /* Linked-list support */
    struct PCB *next;

} PCB;

/* Resource event read from resources.txt */
typedef struct {
    int pid;
    char resource_id[32];
    int capture_time;
    int release_time;
} ResourceEvent;

#endif
