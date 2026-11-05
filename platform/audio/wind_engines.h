/* The three winds. They are different sounds, not one at three strengths, and
 * share nothing but the speed of the air: still air is a hush and a rare sigh,
 * a breeze is soft moving air and no rumble, a gale a broad rush over open
 * ground. Each adds count samples (at most WIND_BLOCK) of stereo at the
 * ambience rate, at about the same loudness as the others; wind_voice.c says
 * how much of each. A gale has no low boom on purpose: turbulence on the ear
 * is what a helmet hears, not what a strong wind sounds like over nothing. */
#ifndef DOOM_AUDIO_WIND_ENGINES_H
#define DOOM_AUDIO_WIND_ENGINES_H
#include "wind_speed.h"

#define WIND_RATE 11025
#define WIND_BLOCK 63      /* five to a tic */

void WindStill_Run(float *left, float *right, int count);
/* puffs 0 a soft breeze that holds, 1 one that comes and goes. */
void WindBreeze_Run(float *left, float *right, int count, const wind_speed_t *soft, const wind_speed_t *puffing, float puffs);
void WindGale_Run(float *left, float *right, int count, const wind_speed_t *speed);
#endif
