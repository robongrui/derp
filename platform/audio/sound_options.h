/* The F4 Sound settings, handed to the mixer by the 3D scene's applySettings. */
#ifndef DOOM_AUDIO_SOUND_OPTIONS_H
#define DOOM_AUDIO_SOUND_OPTIONS_H

#include <stdint.h>

/* Frame trace timing on the game thread. The optional clock returns nanoseconds. */
void I_SetAudioTiming(uint64_t (*clock)(void));
uint64_t I_BeginAudioTiming(void);
void I_EndAudioTiming(uint64_t start);
uint64_t I_TakeAudioTiming(void);

/* acoustics: room echoes and tail, at amount (0-2). walls: muffle sounds out
   of sight. distance: dull far sounds. falloff: far sounds fade as in air,
   not in DOOM's straight line (where the game allows it). */
void I_SetSoundOptions(int acoustics, float amount, int walls, int distance, int falloff);
/* Authored episodes on (F4 Game, Mode): on the game's own maps sound carries
   through the whole level (audio/level_sound.h), with up to far_rooms (0-3,
   F4 Sound, Hearing) far rooms ringing at once, and the room around the
   listener from a grid baked at level start (grid, audio/room_grid.h) or
   measured live from rays (0). */
void I_SetLevelSound(int authored, int far_rooms, int grid);
/* Authored maps: each room's tail traced from the map at level start
   (audio/room_tails.h), or reverb.c's everywhere (0). */
void I_SetRoomTails(int traced);
/* What the sound comes out of: 0 speakers, past that headphones and the head
   heard through (audio/hrtf.h, one past its HRTF_ number). */
void I_SetSoundOutput(int output);
/* How effects are read between their samples: 0 held as in the original,
   1 a cubic, 2 a windowed sinc (see audio/resample.h). Past held samples,
   treble (0-2) is how much made-up treble goes above them (audio/treble.h). */
void I_SetSfxPlayback(int mode, float treble);

/* The made-up sounds (audio/made_bank.h). impacts: sparks, dying lamps and
   screens, breaking glass; glass 0 the shatter, 1 the fracture; ambient: the
   sounds of lamps, machines, liquids, fire and wind, at amount (0-2); steps:
   the player's footsteps and splashes, and monsters splashing. */
void I_SetMadeSounds(int impacts, int glass, int ambient, float amount, int steps);
/* Computer ambience: machine hum, hard-drive seeks and relay chatter instead of bleeps. */
void I_SetAlternativeComputerSounds(int enabled);
/* A made-up effect (an IMPACT_ kind of audio/impact_synth.h) at a map point. */
void I_MadeSound(int kind, float x, float y, float z);
/* The same at scale times its usual level (a contact, by how hard it struck). */
void I_MadeSoundLevel(int kind, float x, float y, float z, float scale);
/* Fixtures shot out around a map point: their ambient sound stops. */
void I_AmbienceSilence(float x, float y, float z, float radius);
/* A window pane on a map line, whole or broken: through a whole one ambience is muffled. */
void I_AmbienceGlaze(int line, int whole);
/* Whether window glass is shown; without it the wind blows through every window. */
void I_AmbienceShowGlass(int shown);
/* How hard it rains (0-1, scene3d/weather.cpp): the rain is heard where the wind is,
   and on the window glass until it breaks. */
void I_AmbienceRain(float amount);
/* How thick it snows (0-1, scene3d/weather.cpp): out under the sky echoes
   fade and dull, and the wind softens. */
void I_AmbienceSnow(float amount);
/* How hard the wind blows (0 still air, 0.5 a breeze, 1 a gale;
   scene3d/wind.cpp windAmount): which of the wind's takes plays, and how loud. */
void I_AmbienceWind(float amount);
/* The drafts' wind where the listener stands indoors (0-1 against the wind
   outside, scene3d/rooms.cpp roomWind; 0 under the sky or with none known):
   the wind is heard on indoors, fading from the openings it comes in by. */
void I_AmbienceDraft(float amount);
/* How many of the mixer's 32 effect channels play now (the FPS readout). */
int I_SoundVoices(void);

/* What plays the music; see platform/i_music.c. The system synthesizer
   is the Mac's own; elsewhere that choice plays the SoundFont. */
enum { MUSIC_SOUNDFONT, MUSIC_OPL, MUSIC_SYSTEM };
void I_SetMusicDevice(int device);
/* A console soundtrack in place of the scores, where its pack has the song
   (platform/music_pack.c): 0 the PC scores, 1 PlayStation, 2 3DO, 3 IDKFA
   remix, 4 recorded originals (the last two from extras.wad). Whether
   that soundtrack's pack was found. */
void I_SetSoundtrack(int soundtrack);
int I_SoundtrackFound(int soundtrack);
#endif
