/* The library's acoustics (episode 0.5, Game_InLibrary): among the books it
 * is dry and dark, the room room.c measures cut short and dulled; and the
 * chasm is the space beyond (beyond.h), an abyss with a long, faint, dark
 * tail, heard as far as the listener stands open to it (Game_LibraryChasm).
 * Nothing of this outside the library. */
#ifndef DOOM_AUDIO_LIBRARY_ACOUSTICS_H
#define DOOM_AUDIO_LIBRARY_ACOUSTICS_H
#include "beyond.h"

/* After Room_Measure and Beyond_Measure, before the reverbs take them. */
void LibraryAcoustics_Shape(room_t *room, beyond_t *beyond);
#endif
