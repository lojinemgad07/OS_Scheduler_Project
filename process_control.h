#ifndef PROCESS_CONTROL_H
#define PROCESS_CONTROL_H

#include "process.h"

/* Create and start a new process */
int startProcess(PCB *process);

/* Stop a currently running process */
int stopProcess(PCB *process);

/* Resume a previously stopped process */
int resumeProcess(PCB *process);

/* Mark a process as blocked */
int blockProcess(PCB *process);

/* Return a blocked process to Ready */
int unblockProcess(PCB *process);

/* Handle a process that has already terminated */
int finishProcess(PCB *process);

#endif
