/* The air rushing by while falling (the library's chasm, Game_LibraryFallSpeed):
 * noise, soft and dull as the fall starts, louder and brighter as it speeds
 * up, a deep roar under it at speed, buffeted a little differently in each
 * ear, and the world's own sound pressed under it. It comes up with the fall
 * and dies away slowly after. */
#ifndef DOOM_AUDIO_FALLING_AIR_H
#define DOOM_AUDIO_FALLING_AIR_H

/* Into an interleaved stereo buffer of count frames at rate, after the
   effects and their reverb, before the music; speed in map units a tic. */
void FallingAir_Mix(float *buffer, int count, int rate, float speed);
#endif
