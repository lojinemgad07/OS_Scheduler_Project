#ifndef IPC_H
#define IPC_H

#include <sys/types.h>

#define SCHEDULER_QUEUE_KEY 0x534348
#define MSG_PROCESS 1L
#define MSG_RESOURCES 2L
#define MSG_END 3L
#define MSG_ERROR 4L
#define MSG_TIME 5L

typedef struct {
    long type;
    int id;
    int arrival;
    int runtime;
    int priority;
    int resource_count;
    char resource_id[32];
    int capture_time;
    int release_time;
} SchedulerMessage;

#endif
