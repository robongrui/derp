/* What each room of the level sounds like, measured once at the start of an
 * authored level (level_sound.h) so a sound made anywhere can ring as its
 * own room does. A room is the room map's (rooms.c): its rooms and porches,
 * and outside its yards. A doorway or window frame sounds as the biggest
 * room next to it. Each record is room.c's own measurement at the room's
 * best spot: where it reads most as a tunnel, if it is one (a mean would
 * smear the tube) and rings longest, else where it holds the most space. A window breaking
 * measures the rooms on both sides again. */
#ifndef DOOM_AUDIO_ROOM_RECORDS_H
#define DOOM_AUDIO_ROOM_RECORDS_H
#include "room.h"

typedef struct { room_t room; float x, y, volume; int sector; } room_record_t;

/* Once a tic on an authored level: measures a new level's rooms, or the
   ones a window just opened or closed, nearest first, a little each tic.
   A room not measured yet has no record (its sounds ring in the
   listener's room, as everywhere before). */
void RoomRecords_Follow(void);
/* Measures every room still waiting at once (doom-room-probe). */
void RoomRecords_Finish(void);
/* The room a sector sounds as, -1 for none. */
int RoomRecords_Key(int sector);
/* The same for a map point. */
int RoomRecords_KeyAt(float x, float y);
/* That room's record, or NULL. */
const room_record_t *RoomRecords_Get(int key);
/* The rooms next to room key that sound passes into (a door must be open, a
   gap tall enough), widest openings first: up to max of them, with the
   width of their openings. */
int RoomRecords_Neighbours(int key, int *keys, float *widths, int max);
/* Measures every room again at the next follow (window panes now count, or no longer). */
void RoomRecords_Reset(void);
/* A window pane on line broke or came back. */
void RoomRecords_Glass(int line);
#endif
