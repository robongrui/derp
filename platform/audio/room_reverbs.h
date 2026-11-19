/* Far rooms ringing, on authored maps (level_sound.h). A sound made in
 * another room than the listener's (and not in the space beyond, beyond.h)
 * rings in a reverb of its own room's record (room_records.h), and that
 * tail reaches the listener the way the map lets it: quieter, duller and
 * from the opening it comes through, as one voice. A few rooms at a time;
 * a room keeps its reverb until its tail has died away. */
#ifndef DOOM_AUDIO_ROOM_REVERBS_H
#define DOOM_AUDIO_ROOM_REVERBS_H
#include "room_records.h"

#define ROOM_SLOTS 3

void RoomReverbs_Init(int rate);
void RoomReverbs_SetEars(int headphones);
/* Forgets every room and all that rings in them (call with the stream locked). */
void RoomReverbs_Clear(void);

/* Game thread, with the stream locked. A reverb for the room key (its
   record), the one it has or a free one: its slot, or -1 when all ring for
   others. Counts as fed now. */
int RoomReverbs_Claim(int key, const room_record_t *record);
/* Frees the reverbs whose rooms have rung out. */
void RoomReverbs_Expire(void);
/* The room a slot rings for, or -1. */
int RoomReverbs_Key(int slot);
/* How a slot's room reaches the listener now: loudness, lowpass and DOOM's
   separation (1 left .. 128 ahead .. 254 right). */
void RoomReverbs_SetPath(int slot, float gain, float cutoff, int separation);

/* Game thread, unlocked: how a room at record reaches the listener, as
   RoomReverbs_SetPath wants it, and where from (x, y). */
void RoomReverbs_Path(const room_record_t *record, float *gain, float *cutoff, float *from_x, float *from_y);

/* How many of the slots may ring (F4 Sound, Hearing: 3, 1 or 0). */
void RoomReverbs_SetCount(int count);
/* How much of the listener's own sounds (their gun) spills into a slot's
   room through the openings between: 0 none. */
void RoomReverbs_SetOwn(int slot, float amount);

/* Mixer: a block starts; each slot's send to add sounds into (NULL if
   free); the listener's own sounds, spilt into the rooms next door; the
   slots' tails added to stereo at amount (the mixer ducks it in a fight). */
void RoomReverbs_Begin(int count);
float *RoomReverbs_Send(int slot);
void RoomReverbs_AddOwn(const float *own, int count);
void RoomReverbs_Mix(float *stereo, int count, float amount);
#endif
