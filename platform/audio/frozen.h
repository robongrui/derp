/* Where the cold has frozen things, for the made-up sounds. The renderer's
 * scene3d/water_ice.cpp sets these; NULL (no renderer) means never. Water
 * frozen over at (x, y): steps there are on ice, it neither laps nor
 * splashes. The air below freezing at (x, y): nothing drips there. */
#ifndef DOOM_AUDIO_FROZEN_H
#define DOOM_AUDIO_FROZEN_H
extern int (*Frozen_Water)(float x, float y);
extern int (*Frozen_Air)(float x, float y);
#endif
