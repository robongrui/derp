/* A room's traced tail heard as one voice from afar (room_reverbs.h, and the
 * space beyond in room_tails.h): mono, at a quarter of the mixer's rate,
 * through the room's mono response (RoomTails_Far). The input's history is
 * kept whether a response plays or not, so a response arriving or changing
 * picks up what is already ringing. */
#ifndef DOOM_AUDIO_FAR_TAIL_H
#define DOOM_AUDIO_FAR_TAIL_H
#include "convolver.h"

#define FAR_DOWN 4  /* a quarter of the mixer's rate */

typedef struct far_tail_s far_tail_t;

/* For responses up to seconds long at rate (the slow one). */
far_tail_t *FarTail_New(float seconds, int rate);
void FarTail_Clear(far_tail_t *tail);
/* n slow samples in; out gets as many through response (zeros for NULL). */
void FarTail_Run(far_tail_t *tail, const conv_slot_t *response, const float *in, float *out, int n);
#endif
