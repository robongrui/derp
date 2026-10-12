/* Room acoustics: what the reverb hears of the room around the listener,
 * measured from the map by room.c and played by reverb.c. */
#ifndef DOOM_AUDIO_ROOM_H
#define DOOM_AUDIO_ROOM_H

/* Eight wall echoes around the listener, then one off the ceiling. */
#define ROOM_TAPS 9

typedef struct {
    float delay[ROOM_TAPS];  /* round trip to the wall, seconds */
    float gain[ROOM_TAPS];   /* echo level: distance and wall material */
    float pan[ROOM_TAPS];    /* -1 left .. 1 right, as the listener faces */
    float decay;             /* seconds for the late tail to fall 60 dB */
    float damping;           /* 0 bright (metal) .. 1 dark (wood, flesh) */
    float late;              /* late tail level */
} room_t;

/* Measures the room around the console player; a dry room outside levels. */
void Room_Measure(room_t *room);
#endif
