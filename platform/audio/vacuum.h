/* Physically accurate Phobos (Game_Phobos): sound needs air. A sound is
 * carried as far as the thinner air at either end allows (Game_Air), and the
 * acoustic graph finds no way through vacuum. What vacuum leaves: your own
 * sounds through the suit, and the floor under you passing on a heavy thud
 * close by. Off Phobos everything is carried whole. */
#ifndef DOOM_AUDIO_VACUUM_H
#define DOOM_AUDIO_VACUUM_H
#include "m_fixed.h"

/* How much of a sound at (x, y) reaches the listener (0-1), and the most
   treble left (Hz) in *cutoff. own: the listener's own sound. */
float Vacuum_Carry(fixed_t x, fixed_t y, int own, float *cutoff);
/* Keeps the acoustic graph on the game's air; every user calls this first. */
void Vacuum_Follow(void);
#endif
