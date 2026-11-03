/* A gamepad's rumble (gamepad_rumble.c), driven by what the player hears:
   their own guns, saw, pain, landings and death by hand (Game_SoundRumble),
   everything else from the bass of its own sound, as loud as it reaches
   them. Output only: it reads the sounds as they start and never touches
   the game. */
#ifndef DOOM_GAMEPAD_RUMBLE_H
#define DOOM_GAMEPAD_RUMBLE_H
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Gamepad;
/* At load: sound id's rumble, from its 8-bit samples' low band (the low
   motor) and its hits (the high one), one value a tic. */
void Rumble_Analyse(int id, const unsigned char *samples, int length, int rate);
/* A sound started: gain is how much of it reaches the player (0-1), own
   whether it is theirs. */
void Rumble_Sound(int id, float gain, int own);
/* Once a frame: the motors' mix sent to the pad. strength: 0 off, 1 low,
   2 full; triggers: the trigger motors too; world: rumble from sounds not
   the player's. */
void Rumble_Frame(struct SDL_Gamepad *pad, float seconds, int strength, int triggers, int world);
void Rumble_Stop(struct SDL_Gamepad *pad);
#ifdef __cplusplus
}
#endif
#endif
