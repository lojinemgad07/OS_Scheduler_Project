#ifndef RR_H
#define RR_H

#include "process.h"

void rrArrived(PCB *process);
PCB *rrSelectNext(void);
void rrDispatched(PCB *process);
int rrShouldPreempt(PCB *running, int quantum, int quantum_used);
void rrPreempted(PCB *process);
void rrCleanup(void);

#endif