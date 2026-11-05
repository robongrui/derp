/* How fast the air is moving against the steady wind (1), a value a tic. The
 * ground is nearly steady; what happens happens in squalls, rare, clustered,
 * hitting in a second and dying over several. Stepped at the game's 35 Hz
 * like the rest of the weather, and only heard: nothing in the world reads it. */
#ifndef DOOM_AUDIO_WIND_SPEED_H
#define DOOM_AUDIO_WIND_SPEED_H
#include "synth_kit.h"

#define WIND_TIC_RATE 35
#define WIND_SPEED_PAST 64   /* tics kept, for what hears a gust a little later */
#define WIND_SQUALLS 8       /* at once; more than this is a cluster anyway */

typedef struct {
    float intensity;   /* how far the ground wanders */
    float wait;        /* shortest seconds between squalls */
    float cluster;     /* lower: longer lulls, tighter clusters */
    float squall;      /* how hard one leans */
    float scale;       /* the whole against the steady wind */
} wind_gusts_t;

typedef struct { float born, big, rise, fall; } wind_squall_t;

typedef struct {
    wind_gusts_t gusts;
    rng_t r;
    float stage[3], calm, seconds, next;
    wind_squall_t squalls[WIND_SQUALLS];
    float past[WIND_SPEED_PAST];
    int at;
} wind_speed_t;

void WindSpeed_Start(wind_speed_t *s, const wind_gusts_t *gusts, unsigned seed);
void WindSpeed_Tic(wind_speed_t *s);
/* The speed so many seconds ago, read smoothly between tics. */
float WindSpeed_Ago(const wind_speed_t *s, float seconds);
#endif
