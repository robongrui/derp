/* A room's tail traced from the map (PLAN-convolution.md). Rays of sound
 * leave a point every way and bounce off the walls, floors and ceilings as
 * the map has them, losing what each surface's material takes (room.c's
 * table), into open sky and out of the map. What passes back by the point
 * after two bounces or more is counted by when it arrives, in which band and
 * from which way round the listener. The first bounces are room.c's echoes,
 * so they are left out here. Past the traced time the tail goes on as the
 * traced part was falling. Safe on any thread once Room_Prepare has run;
 * reads only the level. */
#ifndef DOOM_AUDIO_ROOM_TRACE_H
#define DOOM_AUDIO_ROOM_TRACE_H

#define TRACE_BANDS 3       /* below 500 Hz, 500 to 2000, 2000 to 7000 */
#define TRACE_WAYS 8        /* from where, as map angles turn: way k comes from k * 45 degrees */
#define TRACE_BIN 0.005f    /* seconds a bin */
#define TRACE_BINS 600      /* 3 s */
/* Energies are to the sound itself as heard this far from it, map units. */
#define TRACE_REFERENCE 64.0f

typedef struct {
    float energy[TRACE_BINS][TRACE_BANDS][TRACE_WAYS];
    float decay[TRACE_BANDS];   /* seconds for each band to fall 60 dB */
    int traced;                 /* bins traced; the later ones go on as the traced part fell */
    int bins;                   /* bins until the tail is 70 dB down */
    int rays;                   /* rays sent */
    float escaped;              /* the share of the sound lost to the sky or out of the map */
} room_trace_t;

/* Traces the room round (x, y), from an ear above its floor, with rays
   rays, seeded by seed (each room its own). glass (or NULL: none) marks
   each line with a whole window pane, which sends the sound back. */
void RoomTrace_At(float x, float y, int rays, unsigned seed, const unsigned char *glass, room_trace_t *out);
#endif
