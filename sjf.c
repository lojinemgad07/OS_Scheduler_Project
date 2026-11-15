#include "sjf.h"

#include <stdio.h>
#include <stdlib.h>

static PCB **heap = NULL;
static size_t heap_size = 0;
static size_t heap_cap = 0;

/* Returns nonzero if a has strictly higher SJF priority than b. */
static int sjfBefore(const PCB *a, const PCB *b)
{
    if (a->runtime != b->runtime)
    {
        return a->runtime < b->runtime;
    }
    if (a->arrival_time != b->arrival_time)
    {
        return a->arrival_time < b->arrival_time;
    }
    return a->id < b->id;
}

static void swapNodes(size_t i, size_t j)
{
    PCB *tmp = heap[i];
    heap[i] = heap[j];
    heap[j] = tmp;
}

static void siftUp(size_t idx)
{
    while (idx > 0)
    {
        size_t parent = (idx - 1) / 2;
        if (!sjfBefore(heap[idx], heap[parent]))
        {
            break;
        }
        swapNodes(idx, parent);
        idx = parent;
    }
}

static void siftDown(size_t idx)
{
    for (;;)
    {
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;
        size_t best = idx;

        if (left < heap_size && sjfBefore(heap[left], heap[best]))
        {
            best = left;
        }
        if (right < heap_size && sjfBefore(heap[right], heap[best]))
        {
            best = right;
        }
        if (best == idx)
        {
            break;
        }
        swapNodes(idx, best);
        idx = best;
    }
}

void sjfArrived(PCB *process)
{
    if (!process)
    {
        return;
    }

    if (heap_size == heap_cap)
    {
        size_t new_cap = heap_cap ? heap_cap * 2 : 16;
        PCB **grown = realloc(heap, new_cap * sizeof(*heap));
        if (!grown)
        {
            perror("sjf realloc");
            exit(1);
        }
        heap = grown;
        heap_cap = new_cap;
    }

    heap[heap_size] = process;
    siftUp(heap_size);
    heap_size++;
}

PCB *sjfSelectNext(void)
{
    while (heap_size > 0 && heap[0]->state != READY)
    {
        heap[0] = heap[--heap_size];
        if (heap_size > 0)
        {
            siftDown(0);
        }
    }
    return (heap_size > 0) ? heap[0] : NULL;
}

void sjfDispatched(PCB *process)
{
    if (heap_size == 0)
    {
        return;
    }

    if (heap[0] == process)
    {
        heap[0] = heap[--heap_size];
        if (heap_size > 0)
        {
            siftDown(0);
        }
        return;
    }

    /* Fallback if a non-root node is dispatched */
    for (size_t i = 0; i < heap_size; ++i)
    {
        if (heap[i] == process)
        {
            heap[i] = heap[--heap_size];
            if (i < heap_size)
            {
                siftUp(i);
                siftDown(i);
            }
            return;
        }
    }
}

void sjfCleanup(void)
{
    free(heap);
    heap = NULL;
    heap_size = 0;
    heap_cap = 0;
}