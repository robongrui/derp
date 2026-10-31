/* The room map: what the level's spaces are, worked out once at level load
 * from the map alone (no renderer, no settings), for every simulation that
 * asks whether a place is indoors: the drafts, the sky's enclosure, the
 * window glass, later the weather and the sound. Render-only.
 *
 * Every sector not under the sky is one of:
 * - a room: an indoor space, several sectors joined where they open wide
 *   into each other (a pedestal, a step, a strip of other light);
 * - an opening: a door, a window frame or a thin recess between spaces,
 *   part of the way from one to the next, not a space of its own;
 * - sheltered: under a roof but outside (a porch, a covered walkway): much
 *   of its edge open to the sky.
 * Sky sectors are outside. Joins are cautious (a missed join costs little,
 * the drafts mix air between rooms; a wrong one erases a wall): nothing a
 * line special can move joins, nor anything across a window. */
#ifndef DOOM_ROOMS_H
#define DOOM_ROOMS_H
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ROOM_SKY, ROOM_INDOOR, ROOM_OPENING, ROOM_SHELTERED } room_kind;

/* Builds the map for the level now loaded, if not built yet; cheap after. */
void Rooms_Build(void);
room_kind Rooms_Kind(int sector);
/* Whether a sector is outside: under the sky, or sheltered under a porch.
   The one answer for every system that lays or moves weather. */
int Rooms_Outside(int sector);
/* The space a sector belongs to (0 .. Rooms_Count()-1), shared by the
   sectors of one room or one sheltered space; an opening is its own. -1 for
   sky sectors. */
int Rooms_Of(int sector);
int Rooms_Count(void);
/* The yard a sector lies in (0 .. Rooms_YardCount()-1): the outside (sky
   and sheltered sectors) joined into areas as the rooms join the inside,
   across lines with a real gap, a porch in the yard it opens onto. -1
   indoors, in an opening, or in a sector with no height. */
int Rooms_Yard(int sector);
int Rooms_YardCount(void);
/* Whether the room a sector belongs to opens onto the outside with no door
   and no glass between (straight onto the sky or a porch, or through a
   doorless recess): a tunnel, a garage, a stairwell up to a yard. Nobody
   heats it. 0 for anything that isn't a room. */
int Rooms_Mouth(int sector);
/* How closed in a sector is for heat and sound, 0 (open air) to 1 (a
   room): under the sky by how much of its opening's edge rooms close in (a
   hole in a ceiling; not the sun's figure, which porches close in too), a
   porch by its depth from its open edge. */
float Rooms_Enclosure(int sector);
/* Whether a door special moves the sector (the sector behind a manual door,
   or one a door line tags); if so, top receives the height its ceiling
   opens to, as the game opens it, open or shut now. */
int Rooms_Door(int sector, float *top);

/* Window frames, found in the geometry whatever the glass setting (none in
   a game without glass): a thin frame sector between the space it looks out
   on (outer: the sky, or else the larger) and the room behind (inner, -1 if
   none). In sector order. Its pane lines are the frame's lines onto outer. */
typedef struct { int sector, outer, inner; } rooms_frame;
int Rooms_FrameCount(void);
const rooms_frame *Rooms_Frame(int index);
/* The frame whose pane is on line, or -1. */
int Rooms_PaneFrame(int line);

/* The sky's openings: sky sectors joined by open lines, each opening's edge
   (the length of its lines onto non-sky sectors or walls) and the indoor
   sectors it opens into under a short rim (a ceiling hole's room), each by
   its line's length weighted by how short the rim is (in full, whatever the
   rim, round an opening ringed by roofs over floor running on under it). */
typedef struct { int sector; float length; } rooms_sky_indoor;
int Rooms_SkyOpening(int sector);   /* -1 for sectors under a roof */
int Rooms_SkyOpeningCount(void);
float Rooms_SkyEdge(int opening);
int Rooms_SkyIndoorCount(int opening);
const rooms_sky_indoor *Rooms_SkyIndoor(int opening, int index);

#ifdef __cplusplus
}
#endif
#endif
