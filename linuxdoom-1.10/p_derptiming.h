#ifndef __P_DERPTIMING__
#define __P_DERPTIMING__

#include <stdint.h>

typedef enum
{
    DERP_TIMING_DISABLED = -1,
    DERP_TIMING_NONE,
    DERP_TIMING_NAV,
    DERP_TIMING_SENSE,
    DERP_TIMING_BRAIN,
    DERP_TIMING_COORDINATION,
    DERP_TIMING_COUNT
} derp_timing_group;

// Main-thread scopes. The callback supplies monotonic nanoseconds. Change it
// and drain the counters outside scopes, between frames or tics.
extern uint64_t (*derp_timing_clock)(void);
void P_DerpTimingSet (uint64_t (*clock)(void));
void P_DerpTimingTake (uint64_t times[DERP_TIMING_COUNT]);
derp_timing_group P_DerpTimingSwitch (derp_timing_group next);

static inline derp_timing_group P_DerpTimingBegin (derp_timing_group group)
{
    return derp_timing_clock ? P_DerpTimingSwitch (group) : DERP_TIMING_DISABLED;
}

static inline void P_DerpTimingEnd (derp_timing_group previous)
{
    if (previous != DERP_TIMING_DISABLED)
	P_DerpTimingSwitch (previous);
}

#endif
