/* The library's steps (episode 0.5, Game_InLibrary): every floor there old
 * wood; his own steps heavier and dragging as he tires (Game_LibraryTired);
 * and the people walking near him heard as he is, a step as a foot comes
 * down in their walk (elsewhere only wading monsters are). Nothing of this outside the library. See steps.h. */
#ifndef DOOM_AUDIO_LIBRARY_STEPS_H
#define DOOM_AUDIO_LIBRARY_STEPS_H
#include "steps.h"

struct mobj_s;

/* In the library a dry floor is wood; elsewhere kind as it is. */
int LibrarySteps_Floor(int kind);
/* His step in the library, into events after count (at most max): the
   footfall, his kit, and dragging as he tires; returns the new count.
   Outside the library -1: the caller plays its own step. */
int LibrarySteps_Player(step_event_t *events, int count, int max, int kind, const struct mobj_s *mo, float force);
/* The people walking near him in the library, once a tic. */
int LibrarySteps_People(step_event_t *events, int count, int max);
#endif
