/* Traced room tails, on authored maps (PLAN-convolution.md). At level start
 * a worker traces each room's tail from the map (room_trace.h, room_ir.h)
 * as the rooms are measured (room_records.h), nearest first, as loud as
 * reverb.c's tail would be there. A door, lift or floor that comes to rest
 * somewhere new, or a pane that breaks, has the rooms beside it traced
 * again, and the new tail crossfades in over the old. The listener's room
 * rings through a convolver in place of reverb.c's tail; room.c's echoes
 * stay. Walking into another room crossfades; a room not traced yet keeps
 * reverb.c's tail.
 * The tail runs at a third of the mixer's rate: little of a room's ringing
 * is left above 7 kHz. */
#ifndef DOOM_AUDIO_ROOM_TAILS_H
#define DOOM_AUDIO_ROOM_TAILS_H
#include "convolver.h"

/* Mixer start: the mixer's rate. */
void RoomTails_Init(int rate);
/* F4: traced tails (1) or reverb.c's everywhere (0). Game thread. */
void RoomTails_Set(int on);
/* Once a tic on an authored level, game thread: a new level's tracing
   starts, the rooms measured since are queued. */
void RoomTails_Follow(void);
/* The level ends or loses its sound: the worker stops, the tails go. */
void RoomTails_Stop(void);
/* Game thread, stream locked: the listener is in room key (-1 none),
   facing (radians, a map angle). Whether that room's traced tail plays,
   or is about to (reverb.c's tail is to be quiet). */
int RoomTails_Listen(int key, float facing);
/* Mixer: adds the tail of the mono send to interleaved stereo, times amount. */
void RoomTails_Process(const float *send, float *stereo, int frames, float amount);
/* A window pane on line broke (whole 0) or came back: the rooms either
   side trace again, the pane as it is now. Game thread. */
void RoomTails_Glass(int line, int whole);
/* Mixer, stream locked: forgets what rings. */
void RoomTails_Clear(void);
/* Stream locked: headphones (1) or speakers. On headphones the tail's lows
   are the same in both ears, as a room's are. */
void RoomTails_SetEars(int headphones);

/* Far rooms (room_reverbs.h) and the space beyond ring as their rooms'
   tails too, as one voice at a quarter of the mixer's rate (far_tail.h).
   Game thread: room key's response for that, or NULL (not traced yet, or
   tails off). It stays good until RoomTails_Forget. */
const conv_slot_t *RoomTails_Far(int key);
/* Stream locked, once no far tail plays any (RoomReverbs_Clear): the
   responses traced over or left by the last level are freed. */
void RoomTails_Forget(void);
/* Game thread, stream locked: the space beyond is room key (-1 none),
   heard from DOOM's separation (1 left .. 254 right). Whether its traced
   tail plays (the beyond reverb's tail is to be quiet). */
int RoomTails_Beyond(int key, int separation);
/* Mixer: the space beyond's tail from its send, into stereo, times amount. */
void RoomTails_ProcessBeyond(const float *send, float *stereo, int frames, float amount);
#endif
