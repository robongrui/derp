/* Slow motion for filming; see slow_motion.c. */
#ifndef DOOM_SLOW_MOTION_H
#define DOOM_SLOW_MOTION_H

/* The Slow motion bind (X, F12; g_binds.h): on or off. 0: not allowed (F4 Off, no -slowmotion), the key is the game's as ever. */
int SlowMotion_Toggle(void);
/* The right mouse button, When shooting: slow now, or back to full speed (the click still reaches the game). */
void SlowMotion_Button(void);
/* Each frame: eases the game's clock (I_TimeScale, i_portable_system.c) toward where it should be. */
void SlowMotion_Frame(void);
extern int SlowMotion_Option;
/* How far into slow motion the world is: 0 at full speed, 1 at a quarter (the effects fade with it). */
double SlowMotion_Share(void);
/* Each start and end of slow motion counted, for the mixer's whoosh (slow_whoosh.c). */
extern volatile int SlowMotion_Starts, SlowMotion_Ends;

#endif
