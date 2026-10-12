/* The F4 Sound settings, handed to the mixer by the 3D scene's applySettings. */
#ifndef DOOM_AUDIO_SOUND_OPTIONS_H
#define DOOM_AUDIO_SOUND_OPTIONS_H

/* acoustics: room echoes and tail, at amount (0-2). walls: muffle sounds out
   of sight. distance: dull far sounds. */
void I_SetSoundOptions(int acoustics, float amount, int walls, int distance);
#endif
