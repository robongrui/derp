//
// The kiosk (scp_7179_b/README.md): use facing it opens its menu (m_kiosk.c),
// and whatever he takes there comes back as a tic command (BTS_KIOSK, from
// G_BuildTiccmd to G_Ticker), so recorded games and savegames have it too.
// It all costs nothing, and it doesn't matter: weapons, their ammunition,
// a backpack, health, armour, or ammunition that never runs out. (See
// p_library.h, p_library_local.h.)
//

#include "doomdef.h"
#include "doomstat.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_library_local.h"

boolean P_GiveWeapon (player_t* player, weapontype_t weapon, boolean dropped);
boolean P_GiveArmor (player_t* player, int armortype);

static boolean		used;		// he has used it...
static int		visited;	// ...and the leveltime he last did: the menu opens then
static boolean		endless;	// ammunition that never runs out


void L_KioskLevel (void)
{
    used = false;
    endless = false;
}


// Use, facing the kiosk: its menu, and his place as a note to share.
void L_Kiosk (player_t* player)
{
    used = true, visited = leveltime;
    L_PlaceNote (player);
    L_Log ("kiosk", player->mo);
}


boolean P_LibraryKioskVisited (int* when)
{
    *when = visited;
    return used;
}


boolean P_LibraryEndless (void)
{
    return endless;
}


// Whether this game has the weapon at all (DOOM II's shotgun, not in
// shareware the plasma rifle and the BFG).
boolean P_LibraryHasWeapon (int weapon)
{
    if (weapon == wp_supershotgun && gamemode != commercial)
	return false;
    if ((weapon == wp_plasma || weapon == wp_bfg) && gamemode == shareware)
	return false;
    return weapon >= 0 && weapon < NUMWEAPONS;
}


static void Backpack (player_t* player)
{
    int		i;

    if (player->backpack)
	return;
    for (i = 0; i < NUMAMMO; i++)
	player->maxammo[i] *= 2;
    player->backpack = true;
}


static void Fill (player_t* player)
{
    int		i;

    for (i = 0; i < NUMAMMO; i++)
	player->ammo[i] = player->maxammo[i];
}


// What he took at the kiosk (KIOSK_*), as G_Ticker reads it from his tic
// command.
void P_LibraryKiosk (player_t* player, int order)
{
    int		i;

    if (!P_Library () || !player->mo || player->playerstate != PST_LIVE)
	return;
    if (order >= KIOSK_WEAPON && order < KIOSK_WEAPON + NUMWEAPONS)
    {
	if (!P_LibraryHasWeapon (order - KIOSK_WEAPON))
	    return;
	P_GiveWeapon (player, order - KIOSK_WEAPON, false);
    }
    else if (order >= KIOSK_AMMO && order < KIOSK_AMMO + NUMAMMO)
	player->ammo[order - KIOSK_AMMO] = player->maxammo[order - KIOSK_AMMO];
    else if (order == KIOSK_ALL_AMMO)
	Fill (player);
    else if (order == KIOSK_BACKPACK)
    {
	Backpack (player);
	Fill (player);
    }
    else if (order == KIOSK_HEALTH)
	player->health = player->mo->health = 100;
    else if (order == KIOSK_ARMOR)
	P_GiveArmor (player, 2);
    else if (order == KIOSK_ENDLESS)
	endless = !endless;
    else if (order == KIOSK_EVERYTHING)
    {
	for (i = 0; i < NUMWEAPONS; i++)
	    if (P_LibraryHasWeapon (i))
		player->weaponowned[i] = true;
	Backpack (player);
	Fill (player);
	player->health = player->mo->health = 100;
	P_GiveArmor (player, 2);
    }
    else
	return;
    player->bonuscount += 6;
    S_StartSound (NULL, sfx_itemup);
    L_Log ("took from the kiosk", player->mo);
}


// Each tic: ammunition that never runs out.
void L_KioskTic (player_t* player)
{
    if (endless)
	Fill (player);
}


void L_ArchiveKiosk (void)
{
    L_Put (&used, sizeof used);
    L_Put (&visited, sizeof visited);
    L_Put (&endless, sizeof endless);
}


void L_UnArchiveKiosk (void)
{
    L_Get (&used, sizeof used);
    used = used != 0;
    L_Get (&visited, sizeof visited);
    L_Get (&endless, sizeof endless);
    endless = endless != 0;
}


void L_RebaseKiosk (int off)
{
    visited -= off;
}
