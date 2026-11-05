/* The wind in the mixer: made live, so it never comes round again, and with no
 * 8-bit step. Played on its own bus, which is always read clean whatever the
 * sample playback choice: the hold's sizzle suits a shot, not a bed that never
 * stops. How hard it blows picks which of the three winds is heard and how
 * loud, on a scale a gale stands 22 dB over still air. Run with the audio lock
 * held. */
#ifndef DOOM_AUDIO_WIND_VOICE_H
#define DOOM_AUDIO_WIND_VOICE_H
#include "ambience.h"

/* Where the wind is heard this tic (ambience.c's field for it), and how hard
   it blows, 0 still to 1 a gale. */
void WindVoice_Set(const ambience_voice_t *field, float strength);
/* Whether there is anything to play. */
int WindVoice_Sounding(void);
/* Adds count samples at the ambience rate to left, right and the reverb send. */
void WindVoice_Run(float *left, float *right, float *send, int count);
void WindVoice_Clear(void);
#endif
