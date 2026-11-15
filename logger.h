#ifndef LOGGER_H
#define LOGGER_H

#include "process.h"

int loggerInit(const char *log_path);
void loggerLogStarted(int time, const PCB *p);
void loggerLogResumed(int time, const PCB *p);
void loggerLogStopped(int time, const PCB *p);
void loggerLogFinished(int time, const PCB *p);
int loggerWritePerf(const char *perf_path, const PCB *processes);
void loggerClose(void);

#endif