/* The platform's side of the bindings (game_profile.h's Game_Actions): who
   acts on a key, mouse button or pad button, the names F4's Bindings page
   shows, and changing them there. The game keeps them in its config. */
#ifndef DOOM_BINDINGS_H
#define DOOM_BINDINGS_H
#include "game_profile.h"
#ifdef __cplusplus
extern "C" {
#endif
/* The platform's role bound to code (GAME_ACTION_FLASHLIGHT and on), or
   GAME_ACTION_GAME when the game's own or nobody's. A pad button bound to a
   GAME_ACTION_PRESS gives that, and *pressed the keyboard key it presses. */
game_action_role Bind_Role(int code, int *pressed);
/* The name of a key, mouse button or pad button ("Ctrl", "Mouse 1", "RT"). */
void Bind_Name(int code, char *name, int size);
/* Puts code in an action's slot (0 and 1: keys and mouse buttons, 2: the
   pad). Another action holding it lets go, and *taken says which (-1:
   none). False when it doesn't fit the slot (a pad button in a key slot)
   or is kept for the menus (Esc, F-keys, Tab, Start). */
int Bind_Set(int action, int slot, int code, int *taken);
/* Back to the defaults: one slot, or every action's. */
void Bind_Reset(int action, int slot);
void Bind_ResetAll(void);
int Bind_IsDefault(int action, int slot);
/* Writes the game's config, bindings included. */
void Bind_Save(void);
#ifdef __cplusplus
}
#endif
#endif
