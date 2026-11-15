#ifndef SJF_H
#define SJF_H

#include "process.h"

void sjfArrived(PCB *process);
PCB *sjfSelectNext(void);
void sjfDispatched(PCB *process);
void sjfCleanup(void);

#endif