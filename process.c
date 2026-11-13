#include "headers.h"

static volatile sig_atomic_t wasResumed = 1;

static void handleContinue(int signum)
{
    (void)signum;
    wasResumed = 1;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        fprintf(stderr, "Usage: ./process.out <process_id> <runtime> <message_queue_id>\n");
        return EXIT_FAILURE;
    }

    int processId = atoi(argv[1]);
    int totalRuntime = atoi(argv[2]);
    int queueId = atoi(argv[3]);

    if (processId <= 0 || totalRuntime <= 0 || queueId < 0)
    {
        fprintf(stderr, "Invalid process arguments.\n");
        return EXIT_FAILURE;
    }

    signal(SIGCONT, handleContinue);
    initClk();

    int executedTime = 0;
    int lastClock = getClk();
    wasResumed = 1;

   
    while (executedTime < totalRuntime)
    {
        if (wasResumed)
        {
            lastClock = getClk();
            wasResumed = 0;
        }

        int currentClock = getClk();
        if (currentClock > lastClock)
        {
            executedTime += currentClock - lastClock;
            lastClock = currentClock;

            if (executedTime > totalRuntime)
                executedTime = totalRuntime;
        }
    }

    IPCMessage finishedMessage = {0};
    finishedMessage.mtype = MSG_FINISHED;
    finishedMessage.id = processId;
    finishedMessage.runtime = totalRuntime;

    if (msgsnd(queueId, &finishedMessage, IPC_MESSAGE_SIZE, 0) == -1)
        perror("process: msgsnd completion");

    destroyClk(false);
    return EXIT_SUCCESS;
}
