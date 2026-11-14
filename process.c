#include "headers.h"
#include <errno.h>
#include <limits.h>

static volatile sig_atomic_t resumed;
static void onContinue(int signal_number) { (void)signal_number; resumed = 1; }

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: process.out runtime\n"); return 2; }
    char *end = NULL;
    long remaining = strtol(argv[1], &end, 10);
    if (!end || *end || remaining < 0 || remaining > INT_MAX) {
        fprintf(stderr, "invalid runtime\n"); return 2;
    }
    struct sigaction action;
    action.sa_handler = onContinue;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGCONT, &action, NULL);
    initClk();
    int previous = getClk();
    while (remaining > 0) {
        int now = getClk();
        if (resumed) { previous = now; resumed = 0; }
        if (now > previous) {
            remaining -= now - previous;
            previous = now;
        }
        /* The clock is the simulation source; don't burn a CPU core while it is still. */
        if (remaining > 0 && now == previous) usleep(1000);
    }
    destroyClk(false);
    return 0;
}
