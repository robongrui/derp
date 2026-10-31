/* CPU time for the FPS readout (cpu_time.h): the process's own everywhere,
   the whole system's on Windows. Kept apart from the game's headers, which
   clash with windows.h. */
#include "cpu_time.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double seconds(FILETIME t) {return (double)(((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime) * 1e-7;}
void I_CpuTimes(double *process, double *systemBusy, double *systemTotal)
{
    FILETIME created, exited, kernel, user, idle;
    *process = GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ? seconds(kernel) + seconds(user) : 0;
    /* The system's kernel time includes its idle time. */
    if (GetSystemTimes(&idle, &kernel, &user))
    {
        *systemTotal = seconds(kernel) + seconds(user);
        *systemBusy = *systemTotal - seconds(idle);
    }
    else *systemBusy = *systemTotal = 0;
}
#else
#include <sys/resource.h>
void I_CpuTimes(double *process, double *systemBusy, double *systemTotal)
{
    struct rusage use;
    *process = getrusage(RUSAGE_SELF, &use) == 0
        ? use.ru_utime.tv_sec + use.ru_stime.tv_sec + (use.ru_utime.tv_usec + use.ru_stime.tv_usec) * 1e-6 : 0;
    *systemBusy = *systemTotal = 0;
}
#endif
