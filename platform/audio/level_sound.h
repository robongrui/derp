/* Sound through the whole level, on authored maps (F4 Game, Mode: authored
 * episodes, on the game's own maps; never in the library or on Phobos).
 * DOOM cuts every sound off at 1200 units in a straight line. Here a sound
 * fades by the way the map lets it through instead: by the length of that
 * way, the doors, slits and glass on it and how far round the corners it
 * goes, until it is too faint to hear. A sound too far off for one of
 * DOOM's channels is queued here and heard a moment later, as late as sound
 * takes to come that far. Normal mode never gets here. */
#ifndef DOOM_AUDIO_LEVEL_SOUND_H
#define DOOM_AUDIO_LEVEL_SOUND_H

/* Authored episodes on or off (I_SetLevelSound). */
void LevelSound_Set(int authored);
/* Once a tic: whether this level carries sound now; switches the graph,
   occlusion and room measuring with it. */
int LevelSound_Follow(void);
/* Whether it does, as last followed. */
int LevelSound_On(void);

/* How much of a sound reaches the listener (0-1) along a way length units
   long that loses loss dB, for a sound that carries reach times as far as
   DOOM's own. As DOOM's natural falloff up close, on past 1200 units a
   little faster than spreading alone, and 0 once it is too faint to hear. */
float LevelSound_Gain(float length, float loss, float reach);

/* A sound too far for a channel, waiting to be heard. level is its share of
   the effects volume, reach as for LevelSound_Gain. */
typedef struct { int id; float x, y, z, level, reach; int due; } far_sound_t;
/* Queues a sound that has length units to come: dropped when the same
   sound just went, or too many wait. */
void LevelSound_Queue(int id, float x, float y, float z, float level, float reach, float length);
/* The next queued sound now due, if any. */
int LevelSound_Due(far_sound_t *out);
/* Now and then a sleeping monster somewhere off stirs: its idle sound from
   where it lies, rarely, never the same one twice running. It stays asleep;
   nothing here touches the game or its random numbers. 1 when one is due. */
int LevelSound_Stir(float x, float y, far_sound_t *out);
#endif
