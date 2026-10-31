/* CPU time for the FPS readout's load (scene3d/frame_stats.cpp): the seconds
   this process has run on all cores, and where the system says, the seconds
   all cores were busy and the seconds that passed on all of them together
   (both 0 where it doesn't). Differences between two reads give the load. */
#ifndef DOOM_CPU_TIME_H
#define DOOM_CPU_TIME_H
#ifdef __cplusplus
extern "C" {
#endif
void I_CpuTimes(double *process, double *systemBusy, double *systemTotal);
#ifdef __cplusplus
}
#endif
#endif
