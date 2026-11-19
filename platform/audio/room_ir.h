/* A traced room's tail as sound (PLAN-convolution.md): each band from each
 * way is noise of its own, shaped to the energy room_trace.c counted there,
 * gathered into first-order ambisonics (w all round, x along the map's x,
 * y along its y) so it can be turned to any facing. */
#ifndef DOOM_AUDIO_ROOM_IR_H
#define DOOM_AUDIO_ROOM_IR_H
#include "room_trace.h"

/* Writes frames of the response at rate into w, x and y (frames each);
   seed makes each room's noise its own. The frames past the tail are 0. */
void RoomIR_Build(const room_trace_t *trace, int rate, unsigned seed, float *w, float *x, float *y, int frames);
/* How an ear pointing toward angle (radians, as map angles turn) hears the
   three: gain on w, x and y. Facing f, the left ear points to f + pi/2. */
void RoomIR_Ear(float angle, float *gw, float *gx, float *gy);
#endif
