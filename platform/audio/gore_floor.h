/* Where gore lies on the floor, for the made-up sounds. The renderer's
 * scene3d/body_burst.cpp sets it while the Gore rule is on; NULL (no
 * renderer, or gore off) means nowhere. Blood pooled or pieces lying at
 * (x, y): a step there squelches. */
#ifndef DOOM_AUDIO_GORE_FLOOR_H
#define DOOM_AUDIO_GORE_FLOOR_H
extern int (*Gore_Underfoot)(float x, float y);
#endif
