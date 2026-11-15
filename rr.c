#include "rr.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct RRNode
{
    PCB *pcb;
    struct RRNode *next;
} RRNode;

static RRNode *q_head = NULL;
static RRNode *q_tail = NULL;

static void enqueue(PCB *process)
{
    if (!process)
    {
        return;
    }

    RRNode *node = malloc(sizeof(*node));
    if (!node)
    {
        perror("rr malloc");
        exit(1);
    }

    node->pcb = process;
    node->next = NULL;

    if (q_tail)
    {
        q_tail->next = node;
    }
    else
    {
        q_head = node;
    }
    q_tail = node;
}

static void popHead(void)
{
    if (!q_head)
    {
        return;
    }

    RRNode *old = q_head;
    q_head = q_head->next;
    if (!q_head)
    {
        q_tail = NULL;
    }
    free(old);
}

void rrArrived(PCB *process)
{
    enqueue(process);
}

PCB *rrSelectNext(void)
{
    while (q_head && q_head->pcb->state != READY)
    {
        popHead();
    }
    return q_head ? q_head->pcb : NULL;
}

void rrDispatched(PCB *process)
{
    if (q_head && q_head->pcb == process)
    {
        popHead();
        return;
    }

    /* Defensive removal if a different node was dispatched */
    RRNode *prev = NULL;
    RRNode *curr = q_head;
    while (curr)
    {
        if (curr->pcb == process)
        {
            if (prev)
            {
                prev->next = curr->next;
            }
            else
            {
                q_head = curr->next;
            }
            if (curr == q_tail)
            {
                q_tail = prev;
            }
            free(curr);
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

int rrShouldPreempt(PCB *running, int quantum, int quantum_used)
{
    if (!running || quantum <= 0)
    {
        return 0;
    }
    /* Only preempt when quantum expired AND another process is READY. */
    return (quantum_used >= quantum) && (rrSelectNext() != NULL);
}

void rrPreempted(PCB *process)
{
    enqueue(process);
}

void rrCleanup(void)
{
    while (q_head)
    {
        popHead();
    }
}