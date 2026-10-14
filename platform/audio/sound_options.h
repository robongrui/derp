/* The F4 Sound settings, handed to the mixer by the 3D scene's applySettings. */
#ifndef DOOM_AUDIO_SOUND_OPTIONS_H
#define DOOM_AUDIO_SOUND_OPTIONS_H

/* acoustics: room echoes and tail, at amount (0-2). walls: muffle sounds out
   of sight. distance: dull far sounds. */
void I_SetSoundOptions(int acoustics, float amount, int walls, int distance);
/* How effects are read between their samples: 0 held as in the original,
   1 a cubic, 2 a windowed sinc (see audio/resample.h). Past held samples,
   treble (0-2) is how much made-up treble goes above them (audio/treble.h). */
void I_SetSfxPlayback(int mode, float treble);

/* What plays the music; see platform/i_music.c. The system synthesizer
   is the Mac's own; elsewhere that choice plays the SoundFont. */
enum { MUSIC_SOUNDFONT, MUSIC_OPL, MUSIC_SYSTEM };
void I_SetMusicDevice(int device);
#endif
