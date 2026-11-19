/* Room acoustics: what the reverb hears of the room around the listener,
 * measured from the map by room.c and played by reverb.c. */
#ifndef DOOM_AUDIO_ROOM_H
#define DOOM_AUDIO_ROOM_H
#include <math.h>

/* glibc hides it from the POSIX-only build. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Eight wall echoes around the listener, then one off the ceiling. */
#define ROOM_TAPS 9

typedef struct {
    float delay[ROOM_TAPS];  /* round trip to the wall, seconds */
    float gain[ROOM_TAPS];   /* echo level: distance and wall material */
    float spread[ROOM_TAPS]; /* 0 a clean echo off smooth walls .. 1 smeared by rough ones */
    float dark[ROOM_TAPS];   /* how much treble the echo's walls take: 0 metal .. 1 fabric */
    float pan[ROOM_TAPS];    /* -1 left .. 1 right, as the listener faces */
    float decay;             /* seconds for the late tail to fall 60 dB */
    float room_decay;        /* the same were it no tube (tunnel below): what a space beyond must outlast */
    float damping;           /* 0 bright (metal) .. 1 dark (wood, flesh) */
    float scatter;           /* 0 smooth surfaces, a ringing tail .. 1 rough (stone, dirt), a flat wash */
    float late;              /* late tail level */
    float tunnel;            /* 0 a room .. 1 a long narrow tube: a sewer, a water channel */
    float axis;              /* the tube's direction as the listener faces, radians (left is +) */
    float width, height;     /* its cross-section, map units */
    float flutter_delay;     /* seconds for sound to cross the tube and back */
    float flutter;           /* 0 none .. 1 the strongest ring between its side walls */
    /* The same facing no way (room_grid.h turns a stored room to the listener). */
    float from_x[ROOM_TAPS], from_y[ROOM_TAPS]; /* where each echo comes from, a map direction (pan is it as one faces) */
    float map_axis;          /* the tube's direction as map angles turn */
    float outdoors;          /* how far out in the open air it was measured, 0-1 (the snow's hush) */
} room_t;

/* The rays a measurement casts round its point to find the walls. */
#define ROOM_RAYS 32
/* The floor plan a measurement found: from (x, y), how far the walls are
   along each ray (ray i heads i * 2 pi / ROOM_RAYS radians, as map angles
   turn), and the room's volume in cubic metres. */
typedef struct { float x, y, volume, reach[ROOM_RAYS]; } room_outline_t;

/* Measures the room around the console player; a dry room outside levels.
   outline (or NULL) gets its floor plan. */
void Room_Measure(room_t *room, room_outline_t *outline);
/* The same for an ear above the floor at (x, y), facing (radians, a map angle). */
void Room_MeasureAt(float x, float y, float facing, room_t *room, room_outline_t *outline);
/* Room_MeasureAt with no snow's hush, for a map point rather than the
   listener: safe on any thread once Room_Prepare has run, and the level
   stays loaded. glass (or NULL: none whole) marks each line with a whole pane, as
   Acoustic_GlassWhole on the game thread. rays: how many to cast, a
   multiple of ROOM_RAYS up to 256 (the tube is still read off ROOM_RAYS of
   them; the outline keeps ROOM_RAYS directions, each the farthest of the
   rays round it). inside (or NULL) is how much of the point's surroundings
   are its own sector's: 0 in the void past the walls. */
void Room_MeasureBare(float x, float y, float facing, const unsigned char *glass, int rays, room_t *room,
                      room_outline_t *outline, float *inside);
/* Reads every wall and flat material of the WAD now, on the game thread,
   rather than as rays first meet them. */
void Room_Prepare(void);
/* A room measured facing one way, as heard facing another (radians, a map angle). */
void Room_Face(room_t *room, float facing);
/* Adds the snow's hush (Room_Snow) to a bare measurement. */
void Room_Hush(room_t *room);
/* How thick it snows, 0-1: out under the sky the snow hushes the room. */
void Room_Snow(float amount);
/* Whether whole window panes stop the rays as walls (authored maps,
   level_sound.h); otherwise sound measures through them as open. */
void Room_GlassStops(int on);
/* What a wall texture (flat 0) or a flat (flat 1) is made of to sound, as
   the echoes hear it: the share a bounce loses, how much of its treble, how
   rough it is (0 smooth .. 1 rough). For room_trace.c; after Room_Prepare,
   safe on any thread. */
void Room_Material(int number, int flat, float *absorb, float *dark, float *scatter);
/* The same for a whole window pane: hard and bright. */
void Room_Pane(float *absorb, float *dark, float *scatter);
#endif
