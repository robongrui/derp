/* Footsteps and splashes: the listener's own on every floor, by what the
 * floor is made of, monsters' where they wade through liquids, and under
 * DERP's rules its soldiers' boots on any floor. */
#ifndef DOOM_AUDIO_STEPS_H
#define DOOM_AUDIO_STEPS_H

typedef struct {
    int kind; float x, y, z, level;
    int personal;  /* listener's boot, splash or equipment; not a monster */
} step_event_t;

/* This tic's steps (IMPACT_ kinds of audio/impact_synth.h), at most max;
   returns how many. Call once a tic. */
int Steps_Tic(step_event_t *events, int max);
#endif
