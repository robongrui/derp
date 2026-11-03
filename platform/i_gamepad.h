/* Gamepads and joysticks (i_sdl_gamepad.c). A pad plays through the same
   paths as the keyboard and mouse: its buttons are keys (bind_keys.h)
   bound like any other, its look stick and gyro turn the view as mouse
   motion, and in menus it presses the keys they know. Joysticks SDL has no
   pad layout for go through DOOM's own digital joystick events. */
#ifndef DOOM_I_GAMEPAD_H
#define DOOM_I_GAMEPAD_H
#ifdef __cplusplus
extern "C" {
#endif
union SDL_Event;
/* F4's Controls (scene.cpp's applySettings): look speeds across and up
   and down, the sticks' dead zone, the turn boost, the sticks swapped,
   gyro aim (0 off, 1 on, 2 while the gyro button is held) and its speed,
   and the rumble (0 off, 1 low, 2 full), the triggers' own motors and the
   rumble of what the player hears around them. */
typedef struct {
    float look_x, look_y, deadzone, gyro_speed;
    int boost, southpaw, gyro;
    int rumble, trigger_rumble, world_rumble;
} gamepad_options;
void I_GamepadOptions(const gamepad_options *options);
void I_GamepadInit(void);
void I_GamepadShutdown(void);
/* Takes a pad's or joystick's event: true when it was one. */
int I_GamepadEvent(const union SDL_Event *event);
/* Once a frame: repeats held menu directions, rumbles, and turns the look
   stick and gyro into mouse motion: *turn for an ev_mouse (DOOM's mouse
   units) and *look for I_Render3DLook. view: GAMEPAD_VIEW_OFF when the view
   isn't the player's (menus, F4), _NOW to turn it, _LATER while the 3D
   renderer waits to turn it at its steady moment (I_LatchMouse). */
enum { GAMEPAD_VIEW_OFF, GAMEPAD_VIEW_NOW, GAMEPAD_VIEW_LATER };
void I_GamepadFrame(int view, int *turn, float *look);
/* Lets go of everything held (the window lost focus). */
void I_GamepadReleaseAll(void);
/* The keyboard or mouse was used: a pad no longer was the last. */
void I_GamepadOtherInput(void);
/* A pad button's name as the pad in use labels it (button: 0 to
   KEY_PADBUTTONS-1 SDL's, then the two triggers). */
void I_GamepadButtonName(int button, char *name, int size);
/* Running on a Steam Deck (Steam sets SteamDeck=1). */
int I_GamepadSteamDeck(void);

/* What the pad needs from the window's input (i_sdl_video.c): to post an
   event as the keyboard does, to act as the platform's bound keys do
   (game_profile.h's roles: flashlight, screenshot...; false when it can't,
   and the key goes on to the game), and to open F4. */
void I_PostInput(int type, int data1, int data2, int data3);
int I_PlatformAction(int role);
void I_OpenSettingsPanel(void);
#ifdef __cplusplus
}
#endif
#endif
