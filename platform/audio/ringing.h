/* Ringing ears: after a hard knock (the library's chasm, Game_LibraryRinging)
 * a high steady tone, two close partials beating slowly, and the world muffled
 * under it, all following level (0-1) and fading as he comes to. */
#ifndef DOOM_AUDIO_RINGING_H
#define DOOM_AUDIO_RINGING_H

/* Into an interleaved stereo buffer of count frames at rate, after the
   effects and their reverb, before the music. */
void Ringing_Mix(float *buffer, int count, int rate, float level);
#endif
