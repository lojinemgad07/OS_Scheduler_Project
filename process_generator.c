#include "headers.h"
#include "process.h"
#include "ipc.h"

#include <errno.h>
#include <limits.h>
#include <sys/msg.h>

#define INPUT_LIMIT 100000

typedef struct {
    int pid;
    int capture;
    int release;
    char resource[32];
} ResourceRecord;

static volatile sig_atomic_t interrupted;
static pid_t clock_pid = -1;
static pid_t scheduler_pid = -1;
static int queue_id = -1;

static void onSignal(int sig)
{
    (void)sig;
    interrupted = 1;
}

static int compareProcess(const void *a, const void *b)
{
    const Process *x = a;
    const Process *y = b;

    if (x->arrival != y->arrival) {
        return (x->arrival > y->arrival) - (x->arrival < y->arrival);
    }

    return (x->id > y->id) - (x->id < y->id);
}

static int sendMessage(const SchedulerMessage *message)
{
    while (msgsnd(queue_id, message, sizeof(*message) - sizeof(message->type), 0) == -1) {
        if (errno != EINTR || interrupted) {
            return -1;
        }
    }

    return 0;
}

static int loadProcesses(const char *path, Process **out, size_t *count)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        perror(path);
        return -1;
    }

    Process *items = NULL;
    size_t used = 0;
    size_t capacity = 0;
    char line[256];
    int line_number = 0;

    while (fgets(line, sizeof(line), file)) {
        line_number++;
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        Process process;
        if (sscanf(line, "%d%d%d%d", &process.id, &process.arrival,
                   &process.runtime, &process.priority) != 4 ||
            process.id < 0 || process.arrival < 0 || process.runtime <= 0) {
            fprintf(stderr,
                    "%s:%d: expected id arrival runtime priority\n",
                    path,
                    line_number);
            free(items);
            fclose(file);
            return -1;
        }

        if (used == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            Process *expanded = realloc(items, capacity * sizeof(*items));
            if (!expanded) {
                free(items);
                fclose(file);
                return -1;
            }
            items = expanded;
        }

        items[used++] = process;
        if (used > INPUT_LIMIT) {
            fprintf(stderr, "too many processes\n");
            free(items);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        perror(path);
        free(items);
        fclose(file);
        return -1;
    }

    fclose(file);
    qsort(items, used, sizeof(*items), compareProcess);
    *out = items;
    *count = used;
    return 0;
}

static int loadResources(const char *path, ResourceRecord **out, size_t *count)
{
    FILE *file = fopen(path, "r");
    if (!file && errno == ENOENT) {
        *out = NULL;
        *count = 0;
        return 0;
    }
    if (!file) {
        perror(path);
        return -1;
    }

    ResourceRecord *items = NULL;
    size_t used = 0;
    size_t capacity = 0;
    char line[256];
    int line_number = 0;

    while (fgets(line, sizeof(line), file)) {
        line_number++;
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        ResourceRecord resource;
        if (sscanf(line, "%d%31s%d%d", &resource.pid, resource.resource, &resource.capture, &resource.release) != 4 || resource.capture < 0 || resource.release <= resource.capture) {
            fprintf(stderr, "%s:%d: expected pid resource capture release\n", path, line_number);
            free(items);
            fclose(file);
            return -1;
        }

        if (used == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            ResourceRecord *expanded = realloc(items, capacity * sizeof(*items));
            if (!expanded) {
                free(items);
                fclose(file);
                return -1;
            }
            items = expanded;
        }

        items[used++] = resource;
        if (used > INPUT_LIMIT) {
            fprintf(stderr, "too many resource events\n");
            free(items);
            fclose(file);
            return -1;
        }
    }

    if (ferror(file)) {
        perror(path);
        free(items);
        fclose(file);
        return -1;
    }

    fclose(file);
    *out = items;
    *count = used;
    return 0;
}

static int launch(const char *path, char *const args[])
{
    pid_t child = fork();
    if (child == -1) {
        return -1;
    }

    if (child == 0) {
        execv(path, args);
        perror(path);
        _exit(127);
    }

    return (int)child;
}

static void cleanup(void)
{
    if (scheduler_pid > 0) {
        kill(scheduler_pid, SIGINT);
    }
    if (clock_pid > 0) {
        kill(clock_pid, SIGINT);
    }

    if (scheduler_pid > 0) {
        while (waitpid(scheduler_pid, NULL, 0) == -1 && errno == EINTR) {
        }
    }
    if (clock_pid > 0) {
        while (waitpid(clock_pid, NULL, 0) == -1 && errno == EINTR) {
        }
    }
    if (queue_id >= 0) {
        msgctl(queue_id, IPC_RMID, NULL);
    }
}

int main(void)
{
    struct sigaction signal_action;
    signal_action.sa_handler = onSignal;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;
    sigaction(SIGINT, &signal_action, NULL);
    sigaction(SIGTERM, &signal_action, NULL);

    Process *processes = NULL;
    ResourceRecord *resources = NULL;
    size_t process_count = 0;
    size_t resource_count = 0;
    int algorithm;
    int quantum = 0;
    int context_switch = 0;

    if (loadProcesses("processes.txt", &processes, &process_count) ||
        loadResources("resources.txt", &resources, &resource_count)) {
        goto fail;
    }

    if (!process_count) {
        fprintf(stderr, "processes.txt contains no processes\n");
        goto fail;
    }

    printf("Scheduling algorithm: 1) SJF  2) RR  3) HPF\nChoice: ");
    fflush(stdout);
    if (scanf("%d", &algorithm) != 1 || algorithm < 1 || algorithm > 3) {
        fprintf(stderr, "invalid algorithm\n");
        goto fail;
    }

    if (algorithm == 2) {
        printf("Round Robin quantum: ");
        if (scanf("%d", &quantum) != 1 || quantum <= 0) {
            fprintf(stderr, "quantum must be positive\n");
            goto fail;
        }
    }

    printf("Context-switch time: ");
    if (scanf("%d", &context_switch) != 1 || context_switch < 0) {
        fprintf(stderr, "context-switch time must be nonnegative\n");
        goto fail;
    }

    queue_id = msgget(SCHEDULER_QUEUE_KEY, IPC_CREAT | 0666);
    if (queue_id == -1) {
        perror("msgget");
        goto fail;
    }

    char *clock_args[] = {"./clk.out", NULL};
    clock_pid = launch("./clk.out", clock_args);
    if (clock_pid < 0) {
        perror("launch clock");
        goto fail;
    }

    initClk();

    char algorithm_arg[16];
    char quantum_arg[16];
    char context_arg[16];
    snprintf(algorithm_arg, sizeof(algorithm_arg), "%d", algorithm);
    snprintf(quantum_arg, sizeof(quantum_arg), "%d", quantum);
    snprintf(context_arg, sizeof(context_arg), "%d", context_switch);

    char *scheduler_args[] = {
        "./scheduler.out",
        algorithm_arg,
        quantum_arg,
        context_arg,
        NULL
    };
    scheduler_pid = launch("./scheduler.out", scheduler_args);
    if (scheduler_pid < 0) {
        perror("launch scheduler");
        destroyClk(false);
        goto fail;
    }

    for (size_t i = 0; i < resource_count; ++i) {
        SchedulerMessage message = {0};
        message.type = MSG_RESOURCES;
        message.id = resources[i].pid;
        snprintf(message.resource_id, sizeof(message.resource_id), "%s",
                 resources[i].resource);
        message.capture_time = resources[i].capture;
        message.release_time = resources[i].release;

        if (sendMessage(&message)) {
            goto running_fail;
        }
    }

    size_t next_process = 0;
    while (next_process < process_count && !interrupted) {
        int now = getClk();

        while (next_process < process_count &&
               processes[next_process].arrival <= now) {
            SchedulerMessage message = {0};
            message.type = MSG_PROCESS;
            message.id = processes[next_process].id;
            message.arrival = processes[next_process].arrival;
            message.runtime = processes[next_process].runtime;
            message.priority = processes[next_process].priority;

            if (sendMessage(&message)) {
                goto running_fail;
            }
            next_process++;
        }

        if (next_process < process_count) {
            usleep(1000);
        }
    }

    if (interrupted) {
        goto running_fail;
    }

    SchedulerMessage end_message = {0};
    end_message.type = MSG_END;
    if (sendMessage(&end_message)) {
        goto running_fail;
    }

    destroyClk(false);
    while (waitpid(scheduler_pid, NULL, 0) == -1 && errno == EINTR) {
    }
    scheduler_pid = -1;

    free(processes);
    free(resources);
    cleanup();
    return 0;

running_fail:
    destroyClk(false);
fail:
    free(processes);
    free(resources);
    cleanup();
    return 1;
}
