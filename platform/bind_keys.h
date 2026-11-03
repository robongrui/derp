/* DERP: the codes a binding holds beyond the keyboard's (0-255): mouse
   buttons, the wheel and gamepad buttons, posted as key events like the
   keys (i_sdl_video.c, i_sdl_gamepad.c). Shared by both games' doomdef.h
   and the platform. */
#ifndef DERP_BIND_KEYS_H
#define DERP_BIND_KEYS_H
/* No key, mouse button or pad button. */
#define KEY_NONE		(-1)
/* Mouse buttons 1-5: left, right, middle, back, forward. */
#define KEY_MOUSE1		0x100
#define KEY_MOUSE5		0x104
/* One notch of the wheel: a press and a release in the same moment. */
#define KEY_WHEELUP		0x105
#define KEY_WHEELDOWN		0x106
/* A gamepad button: KEY_PAD + its SDL_GamepadButton (A is KEY_PAD+0), then
   the two triggers pressed past half way. */
#define KEY_PAD			0x110
#define KEY_PADBUTTONS		26
#define KEY_PADLT		(KEY_PAD+KEY_PADBUTTONS)
#define KEY_PADRT		(KEY_PAD+KEY_PADBUTTONS+1)
/* The buttons the defaults use, by where they sit (Xbox labels): A is the
   bottom face button on every pad. */
#define KEY_PAD_A		(KEY_PAD+0)
#define KEY_PAD_B		(KEY_PAD+1)
#define KEY_PAD_X		(KEY_PAD+2)
#define KEY_PAD_Y		(KEY_PAD+3)
#define KEY_PAD_BACK		(KEY_PAD+4)
#define KEY_PAD_START		(KEY_PAD+6)
#define KEY_PAD_L3		(KEY_PAD+7)
#define KEY_PAD_R3		(KEY_PAD+8)
#define KEY_PAD_LB		(KEY_PAD+9)
#define KEY_PAD_RB		(KEY_PAD+10)
#define KEY_PAD_UP		(KEY_PAD+11)
#define KEY_PAD_DOWN		(KEY_PAD+12)
#define KEY_PAD_LEFT		(KEY_PAD+13)
#define KEY_PAD_RIGHT		(KEY_PAD+14)
#define KEY_PAD_R4		(KEY_PAD+16)	/* the right back paddle (Steam Deck R4) */
/* The size of a game's key-down table. */
#define BIND_KEYCOUNT		(KEY_PADRT+1)
#endif
