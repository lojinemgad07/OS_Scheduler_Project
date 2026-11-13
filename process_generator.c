#include "headers.h"

static int messageQueueId = -1;
static pid_t clockPid = -1;
static pid_t schedulerPid = -1;
static int clockAttached = 0;

typedef struct
{
    int id;
    int arrival;
    int runtime;
    int priority;
} ProcessInput;

typedef struct
{
    int pid;
    char resource[RESOURCE_NAME_LEN];
    int captureTime;
    int releaseTime;
} ResourceInput;

static void clearResources(int signum)
{
    (void)signum;

    if (schedulerPid > 0)
        kill(schedulerPid, SIGINT);

    if (clockPid > 0)
        kill(clockPid, SIGINT);

    if (messageQueueId >= 0)
        msgctl(messageQueueId, IPC_RMID, NULL);

    if (clockAttached && shmaddr != NULL && shmaddr != (void *)-1)
        shmdt(shmaddr);

    _exit(EXIT_FAILURE);
}

static int readProcesses(const char *fileName, ProcessInput processes[])
{
    FILE *file = fopen(fileName, "r");
    if (file == NULL)
    {
        perror("Could not open processes.txt");
        return -1;
    }

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        ProcessInput p;
        if (sscanf(line, "%d%d%d%d", &p.id, &p.arrival,
                   &p.runtime, &p.priority) != 4)
        {
            fprintf(stderr, "Invalid line in %s: %s", fileName, line);
            fclose(file);
            return -1;
        }

        if (count >= MAX_PROCESSES)
        {
            fprintf(stderr, "Too many processes (maximum %d).\n", MAX_PROCESSES);
            fclose(file);
            return -1;
        }

        if (p.arrival < 1 || p.runtime <= 0)
        {
            fprintf(stderr, "Invalid arrival/runtime for process %d.\n", p.id);
            fclose(file);
            return -1;
        }

        processes[count++] = p;
    }

    fclose(file);
    return count;
}

static int readResources(const char *fileName, ResourceInput resources[])
{
    FILE *file = fopen(fileName, "r");
    if (file == NULL)
    {
        if (errno == ENOENT)
            return 0;
        perror("Could not open resources.txt");
        return -1;
    }

    char line[256];
    int count = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;

        ResourceInput r;
        if (sscanf(line, "%d%31s%d%d", &r.pid, r.resource,
                   &r.captureTime, &r.releaseTime) != 4)
        {
            fprintf(stderr, "Invalid line in %s: %s", fileName, line);
            fclose(file);
            return -1;
        }

        if (count >= MAX_RESOURCES)
        {
            fprintf(stderr, "Too many resource entries.\n");
            fclose(file);
            return -1;
        }

        if (r.captureTime < 1 || r.releaseTime <= r.captureTime)
        {
            fprintf(stderr, "Invalid resource interval for process %d.\n", r.pid);
            fclose(file);
            return -1;
        }

        resources[count++] = r;
    }

    fclose(file);
    return count;
}

static int sendMessage(const IPCMessage *message)
{
    if (msgsnd(messageQueueId, message, IPC_MESSAGE_SIZE, 0) == -1)
    {
        perror("msgsnd");
        return -1;
    }
    return 0;
}

int main(void)
{
    signal(SIGINT, clearResources);

    ProcessInput processes[MAX_PROCESSES];
    ResourceInput resources[MAX_RESOURCES];

    int processCount = readProcesses("processes.txt", processes);
    if (processCount < 0)
        return EXIT_FAILURE;

    int resourceCount = readResources("resources.txt", resources);
    if (resourceCount < 0)
        return EXIT_FAILURE;

    int algorithm;
    int quantum = 0;
    int contextSwitch = 1;

    printf("Choose Scheduling Algorithm:\n");
    printf("1. Shortest Job First (SJF)\n");
    printf("2. Round Robin (RR)\n");
    printf("3. Highest Priority First (HPF)\n");
    printf("Your choice: ");
    fflush(stdout);

    if (scanf("%d", &algorithm) != 1 ||
        algorithm < ALG_SJF || algorithm > ALG_HPF)
    {
        fprintf(stderr, "Invalid scheduling algorithm.\n");
        return EXIT_FAILURE;
    }

    if (algorithm == ALG_RR)
    {
        printf("Enter quantum: ");
        fflush(stdout);
        if (scanf("%d", &quantum) != 1 || quantum <= 0)
        {
            fprintf(stderr, "Quantum must be a positive integer.\n");
            return EXIT_FAILURE;
        }

        printf("Enter context-switch time: ");
        fflush(stdout);
        if (scanf("%d", &contextSwitch) != 1 || contextSwitch < 0)
        {
            fprintf(stderr, "Context-switch time cannot be negative.\n");
            return EXIT_FAILURE;
        }
    }

    if (algorithm != ALG_RR)
        contextSwitch = 1;

    messageQueueId = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    if (messageQueueId == -1)
    {
        perror("msgget");
        return EXIT_FAILURE;
    }

    clockPid = fork();
    if (clockPid == -1)
    {
        perror("fork clock");
        clearResources(0);
    }
    if (clockPid == 0)
    {
        execl("./clk.out", "clk.out", (char *)NULL);
        perror("execl clk.out");
        _exit(EXIT_FAILURE);
    }

    initClk();
    clockAttached = 1;

    schedulerPid = fork();
    if (schedulerPid == -1)
    {
        perror("fork scheduler");
        clearResources(0);
    }
    if (schedulerPid == 0)
    {
        char queueArg[32], algorithmArg[16], quantumArg[16], switchArg[16];
        snprintf(queueArg, sizeof(queueArg), "%d", messageQueueId);
        snprintf(algorithmArg, sizeof(algorithmArg), "%d", algorithm);
        snprintf(quantumArg, sizeof(quantumArg), "%d", quantum);
        snprintf(switchArg, sizeof(switchArg), "%d", contextSwitch);

        execl("./scheduler.out", "scheduler.out", queueArg,
              algorithmArg, quantumArg, switchArg, (char *)NULL);
        perror("execl scheduler.out");
        _exit(EXIT_FAILURE);
    }

    for (int i = 0; i < resourceCount; i++)
    {
        IPCMessage message = {0};
        message.mtype = MSG_RESOURCE;
        message.id = resources[i].pid;
        message.captureTime = resources[i].captureTime;
        message.releaseTime = resources[i].releaseTime;
        strncpy(message.resource, resources[i].resource,
                RESOURCE_NAME_LEN - 1);

        if (sendMessage(&message) == -1)
            clearResources(0);
    }

    int nextProcess = 0;
    while (nextProcess < processCount)
    {
        int currentTime = getClk();

        while (nextProcess < processCount &&
               processes[nextProcess].arrival <= currentTime)
        {
            IPCMessage message = {0};
            message.mtype = MSG_PROCESS;
            message.id = processes[nextProcess].id;
            message.arrival = processes[nextProcess].arrival;
            message.runtime = processes[nextProcess].runtime;
            message.priority = processes[nextProcess].priority;

            if (sendMessage(&message) == -1)
                clearResources(0);

            nextProcess++;
        }

        if (nextProcess < processCount)
            usleep(1000); 
    }

    IPCMessage endMessage = {0};
    endMessage.mtype = MSG_INPUT_END;
    if (sendMessage(&endMessage) == -1)
        clearResources(0);

    int schedulerStatus;
    while (waitpid(schedulerPid, &schedulerStatus, 0) == -1)
    {
        if (errno == EINTR)
            continue;
        perror("waitpid scheduler");
        break;
    }
    schedulerPid = -1;

    destroyClk(false);
    clockAttached = 0;

    if (clockPid > 0)
    {
        kill(clockPid, SIGINT);
        waitpid(clockPid, NULL, 0);
        clockPid = -1;
    }

    if (messageQueueId >= 0)
    {
        msgctl(messageQueueId, IPC_RMID, NULL);
        messageQueueId = -1;
    }

    printf("Process generator: simulation finished.\n");
    return WIFEXITED(schedulerStatus) ? WEXITSTATUS(schedulerStatus) : EXIT_FAILURE;
}
