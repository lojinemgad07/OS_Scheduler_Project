#include "logger.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static FILE *log_file = NULL;

static int computeWait(int time, const PCB *p)
{
    int wait = time - p->arrival_time - p->executed_time;
    return wait < 0 ? 0 : wait;
}

static double round2(double value)
{
    double r = round(value * 100.0) / 100.0;
    return (r == 0.0) ? 0.0 : r;
}

int loggerInit(const char *log_path)
{
    log_file = fopen(log_path, "w");
    if (!log_file)
    {
        perror(log_path);
        return -1;
    }

    fprintf(log_file,
            "#At time x process y state arr w total z remain y wait k\n");
    fflush(log_file);
    return 0;
}

void loggerLogStarted(int time, const PCB *p)
{
    if (!log_file || !p)
    {
        return;
    }

    fprintf(log_file,
            "At time %d process %d started arr %d total %d remain %d wait %d\n",
            time,
            p->id,
            p->arrival_time,
            p->runtime,
            p->remaining_time,
            computeWait(time, p));
    fflush(log_file);
}

void loggerLogResumed(int time, const PCB *p)
{
    if (!log_file || !p)
    {
        return;
    }

    fprintf(log_file,
            "At time %d process %d resumed arr %d total %d remain %d wait %d\n",
            time,
            p->id,
            p->arrival_time,
            p->runtime,
            p->remaining_time,
            computeWait(time, p));
    fflush(log_file);
}

void loggerLogStopped(int time, const PCB *p)
{
    if (!log_file || !p)
    {
        return;
    }

    fprintf(log_file,
            "At time %d process %d stopped arr %d total %d remain %d wait %d\n",
            time,
            p->id,
            p->arrival_time,
            p->runtime,
            p->remaining_time,
            computeWait(time, p));
    fflush(log_file);
}

void loggerLogFinished(int time, const PCB *p)
{
    if (!log_file || !p)
    {
        return;
    }

    int ta = time - p->arrival_time;
    double wta = (p->runtime > 0) ? ((double)ta / (double)p->runtime) : 0.0;
    int wait = ta - p->runtime;
    if (wait < 0)
    {
        wait = 0;
    }

    fprintf(log_file,
            "At time %d process %d finished arr %d total %d remain 0 wait %d TA %d WTA %.2f\n",
            time,
            p->id,
            p->arrival_time,
            p->runtime,
            wait,
            ta,
            round2(wta));
    fflush(log_file);
}

int loggerWritePerf(const char *perf_path, const PCB *processes)
{
    FILE *perf_file = fopen(perf_path, "w");
    if (!perf_file)
    {
        perror(perf_path);
        return -1;
    }

    int count = 0;
    long long total_runtime = 0;
    long long total_wait = 0;
    double sum_wta = 0.0;
    int last_finish_time = 0;

    for (const PCB *p = processes; p; p = p->next)
    {
        if (p->state != FINISHED)
        {
            continue;
        }
        count++;
        total_runtime += p->runtime;
        if (p->finish_time > last_finish_time)
        {
            last_finish_time = p->finish_time;
        }

        int ta = p->finish_time - p->arrival_time;
        int wait = ta - p->runtime;
        if (wait < 0)
        {
            wait = 0;
        }
        double wta = (p->runtime > 0) ? ((double)ta / (double)p->runtime) : 0.0;

        total_wait += wait;
        sum_wta += wta;
    }

    int cpu_util = 0;
    double avg_wta = 0.0;
    int avg_wait = 0;
    double std_wta = 0.0;

    if (count > 0)
    {
        if (last_finish_time > 0)
        {
            cpu_util = (int)floor(((double)total_runtime * 100.0) / (double)last_finish_time);
        }
        avg_wta = sum_wta / (double)count;
        avg_wait = (int)floor((double)total_wait / (double)count);

        double sq_diff_sum = 0.0;
        for (const PCB *p = processes; p; p = p->next)
        {
            if (p->state != FINISHED)
            {
                continue;
            }
            int ta = p->finish_time - p->arrival_time;
            double wta = (p->runtime > 0) ? ((double)ta / (double)p->runtime) : 0.0;
            double diff = wta - avg_wta;
            sq_diff_sum += diff * diff;
        }
        std_wta = sqrt(sq_diff_sum / (double)count);
    }

    fprintf(perf_file, "CPU utilization = %d%%\n", cpu_util);
    fprintf(perf_file, "Avg WTA = %.2f\n", round2(avg_wta));
    fprintf(perf_file, "Avg Waiting = %d\n", avg_wait);
    fprintf(perf_file, "Std WTA = %.2f\n", round2(std_wta));

    fclose(perf_file);
    return 0;
}

void loggerClose(void)
{
    if (log_file)
    {
        fclose(log_file);
        log_file = NULL;
    }
}