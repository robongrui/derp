// The pickup flash by what the pickup gave: health green, armor blue, keys in their own
// colour, ammo, weapons and the rest in the WAD's own bonus gold. Strength by how much: a
// +1 bonus only touches the screen, a medikit flashes as vanilla did, a soulsphere more.
// Read from the player's numbers before and after, so DEHACKED amounts count as given.
#include <math.h>
#include "doomdef.h"
#include "doomstat.h"
#include "p_local.h"
#include "i_video.h"
#include "p_pickup_flash.h"

// Vanilla's pickup palette (PLAYPAL 10, 6 tics of bonuscount) blends a quarter of the way.
#define VANILLA_AMOUNT 0.25f

static const byte healthColor[3] = {60, 230, 90};
static const byte armorColor[3] = {50, 110, 255};
static const byte cardColors[3][3] = {{40, 90, 255}, {255, 220, 40}, {255, 40, 30}};  // blue, yellow, red

static const byte *flashColor;   // NULL: the WAD's own bonus colour
static float flashAmount;
static boolean given;            // P_PickupFlash set them since the last frame
static int lastCount;

void P_PickupBefore(const player_t *player, pickup_before_t *before)
{
    int i;
    before->health = player->health;
    before->armor = player->armorpoints;
    for (i = 0; i < NUMAMMO; ++i) before->ammo[i] = player->ammo[i];
    for (i = 0; i < NUMCARDS; ++i) before->cards[i] = player->cards[i];
}

void P_PickupFlash(const player_t *player, const pickup_before_t *before)
{
    int i, card = -1;
    float health = (player->health - before->health) / 25.0f;
    float armor = (player->armorpoints - before->armor) / 50.0f;
    float ammo = 0, units;
    if (player != &players[consoleplayer]) return;
    for (i = 0; i < NUMAMMO; ++i)
        if (player->ammo[i] > before->ammo[i])
            ammo += (player->ammo[i] - before->ammo[i]) / (3.0f * clipammo[i]);
    for (i = 0; i < NUMCARDS; ++i)
        if (player->cards[i] && !before->cards[i]) card = i % 3;   // it_bluecard.. then the skulls
    if (health < 0) health = 0;
    if (armor < 0) armor = 0;
    units = health + armor + ammo;
    if (card >= 0) {flashColor = cardColors[card]; units = 1;}
    else if (armor > 0) flashColor = armorColor;
    else if (health > 0) flashColor = healthColor;
    else
    {
        flashColor = NULL;
        // Ammo and weapons by the ammo they gave; a power or the map as vanilla.
        if (units <= 0) units = 1;
    }
    units = sqrtf(units);
    flashAmount = VANILLA_AMOUNT * (units < 0.25f ? 0.25f : units > 1.6f ? 1.6f : units);
    given = true;
}

void P_PickupFlashShow(int bonuscount)
{
    // A flash no pickup gave (the library kiosk's): vanilla's.
    if (bonuscount > lastCount && !given) {flashColor = NULL; flashAmount = VANILLA_AMOUNT;}
    given = false;
    lastCount = bonuscount;
    // Fading over the last BONUSADD (6) tics, as each pickup adds.
    I_PickupTint(flashColor, bonuscount > 0 ? flashAmount * (bonuscount >= 6 ? 1.0f : bonuscount / 6.0f) : 0);
}
