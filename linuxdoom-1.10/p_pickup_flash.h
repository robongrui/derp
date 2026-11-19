// The pickup flash by what the pickup gave (F4 Graphics, Pickup flash): the colour says
// which number went up, the strength how much. Drawing only: the game's bonuscount still
// times the flash, and the playsim never reads any of this.
#ifndef __P_PICKUP_FLASH__
#define __P_PICKUP_FLASH__

#include "d_player.h"

typedef struct
{
    int health, armor;
    int ammo[NUMAMMO];
    boolean cards[NUMCARDS];
} pickup_before_t;

// What the player had before P_TouchSpecialThing hands out the item.
void P_PickupBefore(const player_t *player, pickup_before_t *before);
// After it: the flash's colour and strength from what changed.
void P_PickupFlash(const player_t *player, const pickup_before_t *before);
// ST_doPaletteStuff, each frame: bonuscount when the pickup palette is the one shown, else 0.
void P_PickupFlashShow(int bonuscount);

#endif
