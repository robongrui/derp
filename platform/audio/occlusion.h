/* How much of a positioned sound's treble reaches the listener. */
#ifndef DOOM_AUDIO_OCCLUSION_H
#define DOOM_AUDIO_OCCLUSION_H
#include "m_fixed.h"

#define OCCLUSION_OPEN_HZ 20000.0f

/* A lowpass cutoff in Hz for a sound at (x, y, z). With walls, sounds out of
   sight are muffled; with distance, far ones are dulled. OCCLUSION_OPEN_HZ
   when nothing is lost. */
float Occlusion_Cutoff(fixed_t x, fixed_t y, fixed_t z, int walls, int distance);
/* The height a sound comes from at map units (x, y), given its z: a thing
   from about its middle, a sector's own sound (doors, lifts) from the middle
   of its sector's height. */
float Occlusion_Height(float x, float y, float z);
/* A sound behind a wall leans toward the opening it comes through: where that
   is, and how much (0-1) it leans. 0 in sight, when the way there is about as
   straight, or when there is none and it comes through the wall. */
float Occlusion_Opening(fixed_t x, fixed_t y, fixed_t z, float *way_x, float *way_y);
/* Authored maps (level_sound.h): the whole level is searched, and far
   sounds go on dulling past DOOM's 1200 units. */
void Occlusion_SetFar(int on);
/* How a sound at (x, y, z) gets to the listener: the length of its way, the
   loudness lost on it in dB (doors, slits, glass, a ledge, a long way round,
   a solid wall when there is none) and how much of a detour it is (0
   straight .. 1 half again as long or more). */
typedef struct { float length, loss, detour; } occlusion_path_t;
int Occlusion_Path(fixed_t x, fixed_t y, fixed_t z, occlusion_path_t *out);
#endif
