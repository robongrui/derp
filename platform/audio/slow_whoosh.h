/* Slow motion's whoosh in and out, made up in the mixer (slow_whoosh.c). */
#ifndef DOOM_AUDIO_SLOW_WHOOSH_H
#define DOOM_AUDIO_SLOW_WHOOSH_H

/* Adds to interleaved stereo: a whoosh for each start and end of slow motion
   counted since the last call (slow_motion.h), at full speed whatever the clock. */
void SlowWhoosh_Mix(float *buffer, int count, int rate);
#endif
