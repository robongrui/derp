// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:  none
//
//-----------------------------------------------------------------------------


static const char
rcsid[] = "$Id: g_game.c,v 1.8 1997/02/03 22:45:09 b1 Exp $";

#include <string.h>
#include <stdlib.h>

#include "doomdef.h" 
#include "doomstat.h"

#include "z_zone.h"
#include "f_finale.h"
#include "m_argv.h"
#include "m_misc.h"
#include "m_menu.h"
#include "m_random.h"
#include "i_system.h"
#include "i_video.h"

#include "p_setup.h"
#include "p_saveg.h"
#include "p_library.h"
#include "p_tick.h"

#include "d_main.h"

#include "wi_stuff.h"
#include "hu_stuff.h"
#include "st_stuff.h"
#include "am_map.h"

// Needs access to LFB.
#include "v_video.h"

#include "w_wad.h"

#include "p_local.h" 
#include "p_phobos.h"

#include "s_sound.h"

// Data.
#include "dstrings.h"
#include "sounds.h"

// SKY handling - still the wrong place.
#include "r_data.h"
#include "r_sky.h"



#include "g_game.h"
#include "d_deh.h"


#define SAVESTRINGSIZE	24



boolean	G_CheckDemoStatus (void); 
void	G_ReadDemoTiccmd (ticcmd_t* cmd); 
void	G_WriteDemoTiccmd (ticcmd_t* cmd); 
void	G_PlayerReborn (int player); 
void	G_InitNew (skill_t skill, int episode, int map); 
 
void	G_DoReborn (int playernum); 
 
void	G_DoLoadLevel (void); 
void	G_DoNewGame (void); 
void	G_DoLoadGame (void); 
void	G_DoPlayDemo (void); 
void	G_DoCompleted (void); 
void	G_DoVictory (void); 
void	G_DoWorldDone (void); 
void	G_DoSaveGame (void); 
 
 
gameaction_t    gameaction; 
gamestate_t     gamestate; 
skill_t         gameskill; 
boolean		respawnmonsters;
int             gameepisode; 
int             gamemap; 
 
boolean         paused; 
boolean         sendpause;             	// send a pause event next tic 
boolean         sendsave;             	// send a save event next tic 
int             sendkiosk = -1;       	// something taken at the library's kiosk, next tic (m_kiosk.c)
boolean         usergame;               // ok to save / end game 
 
boolean         timingdemo;             // if true, exit with report on completion 
boolean         nodrawers;              // for comparative timing purposes 
boolean         noblit;                 // for comparative timing purposes 
int             starttime;          	// for comparative timing purposes  	 
 
boolean         viewactive; 
 
boolean         deathmatch;           	// only if started as net death 
boolean         netgame;                // only true if packets are broadcast 
boolean         playeringame[MAXPLAYERS]; 
player_t        players[MAXPLAYERS]; 
 
int             consoleplayer;          // player taking events and displaying 
int             displayplayer;          // view being displayed 
int             gametic; 
int             levelstarttic;          // gametic at level start 
int             totalkills, totalitems, totalsecret;    // for intermission 
 
char            demoname[32]; 
boolean         demorecording; 
boolean         demoplayback; 
boolean		netdemo; 
byte*		demobuffer;
byte*		demo_p;
byte*		demoend; 
boolean         singledemo;            	// quit after playing a demo from cmdline 
 
boolean         precache = true;        // if true, load all graphics at start 
 
wbstartstruct_t wminfo;               	// parms for world map / intermission 
 
short		consistancy[MAXPLAYERS][BACKUPTICS]; 
 
byte*		savebuffer;
 
 
// 
// controls (have defaults) 
// 
// DERP: every action's keys, mouse buttons and pad button (g_binds.h).
#define BIND(name, title, section, role, key, key2, pad) int key_##name, key2_##name, pad_##name;
#define PRESS(name, title, section, key, pad) int pad_##name;
#include "g_binds.h"
#undef BIND
#undef PRESS
 
int             joybfire; 
int             joybstrafe; 
int             joybuse; 
int             joybspeed; 
 
 
 
#define MAXPLMOVE		(forwardmove[1]) 
 
#define TURBOTHRESHOLD	0x32

fixed_t		forwardmove[2] = {0x19, 0x32}; 
fixed_t		sidemove[2] = {0x18, 0x28}; 
fixed_t		angleturn[3] = {640, 1280, 320};	// + slow turn 

#define SLOWTURNTICS	6 
 
// DERP: the keyboard's keys, then the mouse buttons and the pad's (bind_keys.h).
#define NUMKEYS		BIND_KEYCOUNT

boolean         gamekeydown[NUMKEYS]; 
int             turnheld;				// for accelerative turning 
 
// DERP: whether an action's key, mouse button or pad button is held, and
// whether a key event is one of an action's.
static boolean G_Held (int key)
{
    return key >= 0 && key < NUMKEYS && gamekeydown[key];
}
static boolean G_MouseHeld (int key)
{
    return key >= KEY_MOUSE1 && key <= KEY_MOUSE5 && gamekeydown[key];
}
#define HELD(a)		(G_Held (key_##a) || G_Held (key2_##a) || G_Held (pad_##a))
#define MOUSEHELD(a)	(G_MouseHeld (key_##a) || G_MouseHeld (key2_##a))
#define BOUND(k, a)	((k) == key_##a || (k) == key2_##a || (k) == pad_##a)

// DERP: the next (1) or previous (-1) weapon key pressed since the last ticcmd.
static int	weaponstep;

// mouse values are used once 
int             mousex;
int		mousey;         
static int	mouseturns[BACKUPTICS];	// the mouse's part of each ticcmd's turn

int             dclicktime;
int		dclickstate;
int		dclicks; 
int             dclicktime2;
int		dclickstate2;
int		dclicks2;

// joystick values are repeated 
int             joyxmove;
int		joyymove;
boolean         joyarray[5]; 
boolean*	joybuttons = &joyarray[1];		// allow [-1] 
 
int		savegameslot; 
char		savedescription[32]; 
 
 
#define	BODYQUESIZE	32

mobj_t*		bodyque[BODYQUESIZE]; 
int		bodyqueslot; 
 
void*		statcopy;				// for statistics driver
 
 
 
int G_CmdChecksum (ticcmd_t* cmd) 
{ 
    int		i;
    int		sum = 0; 
	 
    for (i=0 ; i< sizeof(*cmd)/4 - 1 ; i++) 
	sum += ((int *)cmd)[i]; 
		 
    return sum; 
} 
 

// DERP: whether the key for weapon slot i (0: fist, 7: chainsaw) is held.
static boolean G_WeaponHeld (int i)
{
    switch (i)
    {
      case 0: return HELD(weapon1);
      case 1: return HELD(weapon2);
      case 2: return HELD(weapon3);
      case 3: return HELD(weapon4);
      case 4: return HELD(weapon5);
      case 5: return HELD(weapon6);
      case 6: return HELD(weapon7);
      case 7: return HELD(weapon8);
    }
    return false;
}

// DERP: the weapon the next (step 1) or previous (-1) weapon key picks: the
// next along DOOM's slots that the player has and has ammo for, as the number
// keys select it (the super shotgun through the shotgun's), or -1.
static int G_NextWeapon (int step)
{
    static const weapontype_t order[] = {wp_fist, wp_chainsaw, wp_pistol, wp_shotgun,
	wp_supershotgun, wp_chaingun, wp_missile, wp_plasma, wp_bfg};
    const int	count = sizeof(order)/sizeof(order[0]);
    player_t*	player = &players[consoleplayer];
    weapontype_t current = player->pendingweapon != wp_nochange
	? player->pendingweapon : player->readyweapon;
    int		i, at = 0;

    for (i = 0; i < count; i++)
	if (order[i] == current)
	    at = i;
    for (i = 1; i < count; i++)
    {
	weapontype_t w = order[((at + step*i) % count + count) % count];
	ammotype_t ammo = weaponinfo[w].ammo;
	if (!player->weaponowned[w]
	    || (ammo != am_noammo && player->ammo[ammo] < (w == wp_bfg ? 40 : w == wp_supershotgun ? 2 : 1))
	    || (w == wp_supershotgun && gamemode != commercial)
	    || ((w == wp_plasma || w == wp_bfg) && gamemode == shareware))
	    continue;
	// The fist's key gives the chainsaw unless berserk with it out.
	if (w == wp_fist && player->weaponowned[wp_chainsaw]
	    && !(player->readyweapon == wp_chainsaw && player->powers[pw_strength]))
	    continue;
	return w == wp_supershotgun ? wp_shotgun : w;
    }
    return -1;
}


//
// G_BuildTiccmd
// Builds a ticcmd from all of the available inputs
// or reads it from the demo buffer. 
// If recording a demo, write it out 
// 
void G_BuildTiccmd (ticcmd_t* cmd) 
{ 
    int		i; 
    boolean	strafe;
    boolean	bstrafe; 
    int		speed;
    int		tspeed; 
    int		forward;
    int		side;
    
    ticcmd_t*	base;

    base = I_BaseTiccmd ();		// empty, or external driver
    memcpy (cmd,base,sizeof(*cmd)); 
	
    cmd->consistancy = 
	consistancy[consoleplayer][maketic%BACKUPTICS]; 

 
    strafe = HELD(strafe) || joybuttons[joybstrafe];
    speed = HELD(speed) || joybuttons[joybspeed];

    // Caps Lock on = always run; the run key then walks.
    {
	static int lastcaps = -1;
	int caps = I_CapsLock ();
	if (lastcaps != -1 && caps != lastcaps)
	    players[consoleplayer].message = caps ? "ALWAYS RUN ON" : "ALWAYS RUN OFF";
	lastcaps = caps;
	speed ^= caps;
    }
 
    forward = side = 0;
    
    // use two stage accelerative turning
    // on the keyboard and joystick
    if (joyxmove < 0
	|| joyxmove > 0  
	|| HELD(right)
	|| HELD(left))
	turnheld += ticdup; 
    else 
	turnheld = 0; 

    if (turnheld < SLOWTURNTICS) 
	tspeed = 2;             // slow turn 
    else 
	tspeed = speed;
    
    // let movement keys cancel each other out
    if (strafe) 
    { 
	if (HELD(right))
	{
	    // fprintf(stderr, "strafe right\n");
	    side += sidemove[speed]; 
	}
	if (HELD(left))
	{
	    //	fprintf(stderr, "strafe left\n");
	    side -= sidemove[speed]; 
	}
	if (joyxmove > 0) 
	    side += sidemove[speed]; 
	if (joyxmove < 0) 
	    side -= sidemove[speed]; 
 
    } 
    else 
    { 
	if (HELD(right))
	    cmd->angleturn -= angleturn[tspeed]; 
	if (HELD(left))
	    cmd->angleturn += angleturn[tspeed]; 
	if (joyxmove > 0) 
	    cmd->angleturn -= angleturn[tspeed]; 
	if (joyxmove < 0) 
	    cmd->angleturn += angleturn[tspeed]; 
    } 
 
    if (HELD(up))
    {
	// fprintf(stderr, "up\n");
	forward += forwardmove[speed]; 
    }
    if (HELD(down))
    {
	// fprintf(stderr, "down\n");
	forward -= forwardmove[speed]; 
    }
    if (joyymove < 0) 
	forward += forwardmove[speed]; 
    if (joyymove > 0) 
	forward -= forwardmove[speed]; 
    if (HELD(straferight))
	side += sidemove[speed]; 
    if (HELD(strafeleft))
	side -= sidemove[speed];

    // DERP: a gamepad's stick moves as far as it is tilted, at full tilt
    // as fast as running.
    {
	float	padforward, padside;
	if (I_GamepadMove (&padforward, &padside))
	{
	    forward += (int)(padforward*forwardmove[1]);
	    side += (int)(padside*sidemove[1]);
	}
    }
    
    // buttons
    cmd->chatchar = HU_dequeueChatChar(); 
 
    if (HELD(fire) || joybuttons[joybfire])
	cmd->buttons |= BT_ATTACK; 
 
    if (HELD(jump))
	cmd->buttons |= BT_JUMP;

    if (HELD(use) || joybuttons[joybuse] )
    { 
	cmd->buttons |= BT_USE;
	// clear double clicks if hit use button 
	dclicks = 0;                   
    } 

    // chainsaw overrides 
    for (i=0 ; i<NUMWEAPONS-1 ; i++)        
	if (G_WeaponHeld (i))
	{ 
	    cmd->buttons |= BT_CHANGE; 
	    cmd->buttons |= i<<BT_WEAPONSHIFT; 
	    break; 
	}
    if (weaponstep && !(cmd->buttons & BT_CHANGE))
    {
	int	weapon = G_NextWeapon (weaponstep);
	if (weapon >= 0)
	    cmd->buttons |= BT_CHANGE | weapon<<BT_WEAPONSHIFT;
    }
    weaponstep = 0;
    
    // forward double click (a mouse button bound to Forward)
    if (MOUSEHELD(up) != dclickstate && dclicktime > 1 )
    { 
	dclickstate = MOUSEHELD(up);
	if (dclickstate) 
	    dclicks++; 
	if (dclicks == 2) 
	{ 
	    cmd->buttons |= BT_USE; 
	    dclicks = 0; 
	} 
	else 
	    dclicktime = 0; 
    } 
    else 
    { 
	dclicktime += ticdup; 
	if (dclicktime > 20) 
	{ 
	    dclicks = 0; 
	    dclickstate = 0; 
	} 
    }
    
    // strafe double click
    bstrafe =
	MOUSEHELD(strafe)
	|| joybuttons[joybstrafe]; 
    if (bstrafe != dclickstate2 && dclicktime2 > 1 ) 
    { 
	dclickstate2 = bstrafe; 
	if (dclickstate2) 
	    dclicks2++; 
	if (dclicks2 == 2) 
	{ 
	    cmd->buttons |= BT_USE; 
	    dclicks2 = 0; 
	} 
	else 
	    dclicktime2 = 0; 
    } 
    else 
    { 
	dclicktime2 += ticdup; 
	if (dclicktime2 > 20) 
	{ 
	    dclicks2 = 0; 
	    dclickstate2 = 0; 
	} 
    } 
 
    forward += mousey;
    // A tic's summed motion must fit angleturn (a short) or a flick turns backwards.
    if (mousex > 4095) mousex = 4095;
    else if (mousex < -4095) mousex = -4095;
    if (strafe)
	side += mousex*2; 
    else 
	cmd->angleturn -= mousex*0x8; 
    mouseturns[maketic%BACKUPTICS] = strafe ? 0 : -mousex*0x8;

    mousex = mousey = 0; 
	 
    if (forward > MAXPLMOVE) 
	forward = MAXPLMOVE; 
    else if (forward < -MAXPLMOVE) 
	forward = -MAXPLMOVE; 
    if (side > MAXPLMOVE) 
	side = MAXPLMOVE; 
    else if (side < -MAXPLMOVE) 
	side = -MAXPLMOVE; 
 
    cmd->forwardmove += forward; 
    cmd->sidemove += side;
    
    // special buttons
    if (sendpause) 
    { 
	sendpause = false; 
	cmd->buttons = BT_SPECIAL | BTS_PAUSE; 
    } 
 
    if (sendsave) 
    { 
	sendsave = false; 
	cmd->buttons = BT_SPECIAL | BTS_SAVEGAME | (savegameslot<<BTS_SAVESHIFT); 
    } 

    // Taken at the library's kiosk (m_kiosk.c).
    if (sendkiosk >= 0)
    {
	cmd->buttons = BT_SPECIAL | BTS_KIOSK | (sendkiosk & BTS_KIOSKMASK);
	sendkiosk = -1;
    }
} 

//
// G_MouseTurn
// For a renderer that turns the view every frame (the 3D one): in *last
// the mouse's part of the newest run tic's turn, in *pending what ticcmds
// built but not run yet and the mouse events still queued will add, both
// as angleturn. *pending is 0 when the next tic won't turn the player
// (dead, frozen after a teleport, the chainsaw's pull) or the mouse strafes.
//
void G_MouseTurn (int* last, int* pending)
{
    player_t*	player = &players[consoleplayer];
    int		x = mousex;
    int		i;
    // Upside down (Phobos) P_MovePlayer turns the other way.
    int		sign = P_ViewUpside () ? -1 : 1;

    *last = gametic ? sign*mouseturns[(gametic-1)%BACKUPTICS] : 0;
    *pending = 0;
    if (player->playerstate != PST_LIVE || !player->mo
	|| player->mo->reactiontime || (player->mo->flags & MF_JUSTATTACKED)
	|| HELD(strafe) || joybuttons[joybstrafe])
	return;
    for (i = eventtail ; i != eventhead ; i = (i+1)&(MAXEVENTS-1))
	if (events[i].type == ev_mouse)
	    x += events[i].data2*(mouseSensitivity+5)/10;
    if (x > 4095) x = 4095;
    else if (x < -4095) x = -4095;
    *pending = -sign*x*0x8;
    for (i = gametic ; i < maketic ; i++)
	*pending += sign*mouseturns[i%BACKUPTICS];
}
 

//
// G_DoLoadLevel 
//
extern  gamestate_t     wipegamestate; 
 
void G_DoLoadLevel (void) 
{ 
    int             i; 

    // Set the sky map.
    // First thing, we have a dummy sky texture name,
    //  a flat. The data is in the WAD only because
    //  we look for an actual index, instead of simply
    //  setting one.
    skyflatnum = R_FlatNumForName ( SKYFLATNAME );

    // DOOM determines the sky texture to be used
    // depending on the current episode, and the game version.
    if (gamemode == commercial)
    {
	skytexture = R_TextureNumForName ("SKY3");
	if (gamemap < 12)
	    skytexture = R_TextureNumForName ("SKY1");
	else
	    if (gamemap < 21)
		skytexture = R_TextureNumForName ("SKY2");
    }

    levelstarttic = gametic;        // for time calculation
    
    if (wipegamestate == GS_LEVEL) 
	wipegamestate = -1;             // force a wipe 

    gamestate = GS_LEVEL; 

    for (i=0 ; i<MAXPLAYERS ; i++) 
    { 
	if (playeringame[i] && players[i].playerstate == PST_DEAD) 
	    players[i].playerstate = PST_REBORN; 
	memset (players[i].frags,0,sizeof(players[i].frags)); 
    } 
		 
    P_SetupLevel (gameepisode, gamemap, 0, gameskill);    
    displayplayer = consoleplayer;		// view the guy you are playing    
    starttime = I_GetTime (); 
    gameaction = ga_nothing; 
    Z_CheckHeap ();
    
    // clear cmd building stuff
    memset (gamekeydown, 0, sizeof(gamekeydown)); 
    joyxmove = joyymove = 0; 
    mousex = mousey = 0; 
    sendpause = sendsave = paused = false; 
    weaponstep = 0;
    memset (joyarray, 0, sizeof(joyarray)); 
} 
 
 
//
// G_Responder  
// Get info needed to make ticcmd_ts for the players.
// 
boolean G_Responder (event_t* ev) 
{ 
    // allow spy mode changes even during the demo
    if (gamestate == GS_LEVEL && ev->type == ev_keydown 
	&& ev->data1 == KEY_F12 && (singledemo || !deathmatch) )
    {
	// spy mode 
	do 
	{ 
	    displayplayer++; 
	    if (displayplayer == MAXPLAYERS) 
		displayplayer = 0; 
	} while (!playeringame[displayplayer] && displayplayer != consoleplayer); 
	return true; 
    }
    
    // any other key pops up menu if in demos
    if (gameaction == ga_nothing && !singledemo && 
	(demoplayback || gamestate == GS_DEMOSCREEN) 
	) 
    { 
	if (ev->type == ev_keydown ||  
	    (ev->type == ev_mouse && ev->data1) || 
	    (ev->type == ev_joystick && ev->data1) ) 
	{ 
	    M_StartControlPanel (); 
	    return true; 
	} 
	return false; 
    } 
 
    if (gamestate == GS_LEVEL) 
    { 
#if 0 
	if (devparm && ev->type == ev_keydown && ev->data1 == ';') 
	{ 
	    G_DeathMatchSpawnPlayer (0); 
	    return true; 
	} 
#endif 
	if (HU_Responder (ev)) 
	    return true;	// chat ate the event 
	if (ST_Responder (ev)) 
	    return true;	// status window ate it 
	if (AM_Responder (ev)) 
	    return true;	// automap ate it 
	// Phobos: the grav boots on or off.
	if (ev->type == ev_keydown && BOUND(ev->data1, boots)
	    && P_ToggleBoots (&players[consoleplayer]))
	    return true;
    } 
	 
    if (gamestate == GS_FINALE) 
    { 
	if (F_Responder (ev)) 
	    return true;	// finale ate the event 
    } 
	 
    switch (ev->type) 
    { 
      case ev_keydown: 
	if (ev->data1 == KEY_PAUSE) 
	{ 
	    sendpause = true; 
	    return true; 
	} 
	if (BOUND(ev->data1, nextweapon))
	    weaponstep = 1;
	if (BOUND(ev->data1, prevweapon))
	    weaponstep = -1;
	if (ev->data1 >= 0 && ev->data1 <NUMKEYS)
	    gamekeydown[ev->data1] = true; 
	return true;    // eat key down events 
 
      case ev_keyup: 
	if (ev->data1 >= 0 && ev->data1 <NUMKEYS)
	    gamekeydown[ev->data1] = false; 
	return false;   // always let key up events filter down 
		 
      case ev_mouse: 
	// DERP: the mouse buttons are keys (bind_keys.h).
	{
	    int	button;
	    for (button = 0; button < 5; button++)
		gamekeydown[KEY_MOUSE1+button] = (ev->data1 >> button) & 1;
	}
	// Uncapped frames post one mouse event each, several per tic:
	// sum them until G_BuildTiccmd uses and clears the motion.
	mousex += ev->data2*(mouseSensitivity+5)/10;
	mousey += ev->data3*(mouseSensitivity+5)/10;
	return true;    // eat events 
 
      case ev_joystick: 
	joybuttons[0] = ev->data1 & 1; 
	joybuttons[1] = ev->data1 & 2; 
	joybuttons[2] = ev->data1 & 4; 
	joybuttons[3] = ev->data1 & 8; 
	joyxmove = ev->data2; 
	joyymove = ev->data3; 
	return true;    // eat events 
 
      default: 
	break; 
    } 
 
    return false; 
} 
 
 
 
//
// G_Ticker
// Make ticcmd_ts for the players.
//
void G_Ticker (void) 
{ 
    int		i;
    int		buf; 
    ticcmd_t*	cmd;
    
    // do player reborns if needed
    for (i=0 ; i<MAXPLAYERS ; i++) 
	if (playeringame[i] && players[i].playerstate == PST_REBORN) 
	    G_DoReborn (i);
    
    // do things to change the game state
    while (gameaction != ga_nothing) 
    { 
	switch (gameaction) 
	{ 
	  case ga_loadlevel: 
	    G_DoLoadLevel (); 
	    break; 
	  case ga_newgame: 
	    G_DoNewGame (); 
	    break; 
	  case ga_loadgame: 
	    G_DoLoadGame (); 
	    break; 
	  case ga_savegame: 
	    G_DoSaveGame (); 
	    break; 
	  case ga_playdemo: 
	    G_DoPlayDemo (); 
	    break; 
	  case ga_completed: 
	    G_DoCompleted (); 
	    break; 
	  case ga_victory: 
	    F_StartFinale (); 
	    break; 
	  case ga_worlddone: 
	    G_DoWorldDone (); 
	    break; 
	  case ga_screenshot: 
	    M_ScreenShot (); 
	    gameaction = ga_nothing; 
	    break; 
	  case ga_nothing: 
	    break; 
	} 
    }
    
    // get commands, check consistancy,
    // and build new consistancy check
    buf = (gametic/ticdup)%BACKUPTICS; 
 
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	if (playeringame[i]) 
	{ 
	    cmd = &players[i].cmd; 
 
	    memcpy (cmd, &netcmds[i][buf], sizeof(ticcmd_t)); 
 
	    if (demoplayback) 
		G_ReadDemoTiccmd (cmd); 
	    if (demorecording) 
		G_WriteDemoTiccmd (cmd);
	    
	    // check for turbo cheats
	    if (cmd->forwardmove > TURBOTHRESHOLD 
		&& !(gametic&31) && ((gametic>>5)&3) == i )
	    {
		static char turbomessage[80];
		extern char *player_names[4];
		sprintf (turbomessage, "%s is turbo!",player_names[i]);
		players[consoleplayer].message = turbomessage;
	    }
			
	    if (netgame && !netdemo && !(gametic%ticdup) ) 
	    { 
		if (gametic > BACKUPTICS 
		    && consistancy[i][buf] != cmd->consistancy) 
		{ 
		    I_Error ("consistency failure (%i should be %i)",
			     cmd->consistancy, consistancy[i][buf]); 
		} 
		if (players[i].mo) 
		    consistancy[i][buf] = players[i].mo->x; 
		else 
		    consistancy[i][buf] = rndindex; 
	    } 
	}
    }
    
    // check for special buttons
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	if (playeringame[i]) 
	{ 
	    if ((players[i].cmd.buttons & (BT_SPECIAL|BTS_KIOSK)) == (BT_SPECIAL|BTS_KIOSK))
		P_LibraryKiosk (&players[i], players[i].cmd.buttons & BTS_KIOSKMASK);
	    else if (players[i].cmd.buttons & BT_SPECIAL) 
	    { 
		switch (players[i].cmd.buttons & BT_SPECIALMASK) 
		{ 
		  case BTS_PAUSE: 
		    paused ^= 1; 
		    if (paused) 
			S_PauseSound (); 
		    else 
			S_ResumeSound (); 
		    break; 
					 
		  case BTS_SAVEGAME: 
		    if (!savedescription[0]) 
			strcpy (savedescription, "NET GAME"); 
		    savegameslot =  
			(players[i].cmd.buttons & BTS_SAVEMASK)>>BTS_SAVESHIFT; 
		    gameaction = ga_savegame; 
		    break; 
		} 
	    } 
	}
    }
    
    // do main actions
    switch (gamestate) 
    { 
      case GS_LEVEL: 
	P_Ticker (); 
	ST_Ticker (); 
	AM_Ticker (); 
	HU_Ticker ();            
	break; 
	 
      case GS_INTERMISSION: 
	WI_Ticker (); 
	break; 
			 
      case GS_FINALE: 
	F_Ticker (); 
	break; 
 
      case GS_DEMOSCREEN: 
	D_PageTicker (); 
	break; 
    }        
} 
 
 
//
// PLAYER STRUCTURE FUNCTIONS
// also see P_SpawnPlayer in P_Things
//

//
// G_InitPlayer 
// Called at the start.
// Called by the game initialization functions.
//
void G_InitPlayer (int player) 
{ 
    player_t*	p; 
 
    // set up the saved info         
    p = &players[player]; 
	 
    // clear everything else to defaults 
    G_PlayerReborn (player); 
	 
} 
 
 

//
// G_PlayerFinishLevel
// Can when a player completes a level.
//
void G_PlayerFinishLevel (int player) 
{ 
    player_t*	p; 
	 
    p = &players[player]; 
	 
    memset (p->powers, 0, sizeof (p->powers)); 
    memset (p->cards, 0, sizeof (p->cards)); 
    p->mo->flags &= ~MF_SHADOW;		// cancel invisibility 
    p->extralight = 0;			// cancel gun flashes 
    p->fixedcolormap = 0;		// cancel ir gogles 
    p->damagecount = 0;			// no palette changes 
    p->bonuscount = 0; 
} 
 

//
// G_PlayerReborn
// Called after a player dies 
// almost everything is cleared and initialized 
//
void G_PlayerReborn (int player) 
{ 
    player_t*	p; 
    int		i; 
    int		frags[MAXPLAYERS]; 
    int		killcount;
    int		itemcount;
    int		secretcount; 
	 
    memcpy (frags,players[player].frags,sizeof(frags)); 
    killcount = players[player].killcount; 
    itemcount = players[player].itemcount; 
    secretcount = players[player].secretcount; 
	 
    p = &players[player]; 
    memset (p, 0, sizeof(*p)); 
 
    memcpy (players[player].frags, frags, sizeof(players[player].frags)); 
    players[player].killcount = killcount; 
    players[player].itemcount = itemcount; 
    players[player].secretcount = secretcount; 
 
    p->usedown = p->attackdown = true;	// don't do anything immediately 
    p->playerstate = PST_LIVE;       
    p->health = deh_initial_health; 
    p->readyweapon = p->pendingweapon = wp_pistol; 
    p->weaponowned[wp_fist] = true; 
    p->weaponowned[wp_pistol] = true; 
    p->ammo[am_clip] = deh_initial_bullets; 
	 
    for (i=0 ; i<NUMAMMO ; i++) 
	p->maxammo[i] = maxammo[i]; 
		 
}

//
// G_CheckSpot  
// Returns false if the player cannot be respawned
// at the given mapthing_t spot  
// because something is occupying it 
//
void P_SpawnPlayer (mapthing_t* mthing); 
 
boolean
G_CheckSpot
( int		playernum,
  mapthing_t*	mthing ) 
{ 
    fixed_t		x;
    fixed_t		y; 
    subsector_t*	ss; 
    unsigned		an; 
    mobj_t*		mo; 
    int			i;
	
    if (!players[playernum].mo)
    {
	// first spawn of level, before corpses
	for (i=0 ; i<playernum ; i++)
	    if (players[i].mo->x == mthing->x << FRACBITS
		&& players[i].mo->y == mthing->y << FRACBITS)
		return false;	
	return true;
    }
		
    x = mthing->x << FRACBITS; 
    y = mthing->y << FRACBITS; 
	 
    if (!P_CheckPosition (players[playernum].mo, x, y) ) 
	return false; 
 
    // flush an old corpse if needed 
    if (bodyqueslot >= BODYQUESIZE) 
	P_RemoveMobj (bodyque[bodyqueslot%BODYQUESIZE]); 
    bodyque[bodyqueslot%BODYQUESIZE] = players[playernum].mo; 
    bodyqueslot++; 
	
    // spawn a teleport fog 
    ss = R_PointInSubsector (x,y); 
    an = ( ANG45 * (mthing->angle/45) ) >> ANGLETOFINESHIFT; 
 
    mo = P_SpawnMobj (x+20*finecosine[an], y+20*finesine[an] 
		      , ss->sector->floorheight 
		      , MT_TFOG); 
	 
    if (players[consoleplayer].viewz != 1) 
	S_StartSound (mo, sfx_telept);	// don't start sound on first frame 
 
    return true; 
} 


//
// G_DeathMatchSpawnPlayer 
// Spawns a player at one of the random death match spots 
// called at level load and each death 
//
void G_DeathMatchSpawnPlayer (int playernum) 
{ 
    int             i,j; 
    int				selections; 
	 
    selections = deathmatch_p - deathmatchstarts; 
    if (selections < 4) 
	I_Error ("Only %i deathmatch spots, 4 required", selections); 
 
    for (j=0 ; j<20 ; j++) 
    { 
	i = P_Random() % selections; 
	if (G_CheckSpot (playernum, &deathmatchstarts[i]) ) 
	{ 
	    deathmatchstarts[i].type = playernum+1; 
	    P_SpawnPlayer (&deathmatchstarts[i]); 
	    return; 
	} 
    } 
 
    // no good spot, so the player will probably get stuck 
    P_SpawnPlayer (&playerstarts[playernum]); 
} 

//
// G_DoReborn 
// 
void G_DoReborn (int playernum) 
{ 
    int                             i; 
	 
    if (!netgame)
    {
	// reload the level from scratch
	gameaction = ga_loadlevel;  
    }
    else 
    {
	// respawn at the start

	// first dissasociate the corpse 
	players[playernum].mo->player = NULL;   
		 
	// spawn at random spot if in death match 
	if (deathmatch) 
	{ 
	    G_DeathMatchSpawnPlayer (playernum); 
	    return; 
	} 
		 
	if (G_CheckSpot (playernum, &playerstarts[playernum]) ) 
	{ 
	    P_SpawnPlayer (&playerstarts[playernum]); 
	    return; 
	}
	
	// try to spawn at one of the other players spots 
	for (i=0 ; i<MAXPLAYERS ; i++)
	{
	    if (G_CheckSpot (playernum, &playerstarts[i]) ) 
	    { 
		playerstarts[i].type = playernum+1;	// fake as other player 
		P_SpawnPlayer (&playerstarts[i]); 
		playerstarts[i].type = i+1;		// restore 
		return; 
	    }	    
	    // he's going to be inside something.  Too bad.
	}
	P_SpawnPlayer (&playerstarts[playernum]); 
    } 
} 
 
 
void G_ScreenShot (void) 
{ 
    gameaction = ga_screenshot; 
} 
 


// DOOM Par Times. Episode 5 (SIGIL) has none of its own: its WAD's
// DEHACKED [PARS] fills them in.
int pars[6][10] =
{ 
    {0}, 
    {0,30,75,120,90,165,180,180,30,165}, 
    {0,90,90,90,120,90,360,240,30,170}, 
    {0,90,45,90,150,90,90,165,30,135} 
}; 

// DOOM II Par Times
int cpars[32] =
{
    30,90,120,120,90,150,120,120,270,90,	//  1-10
    210,150,150,150,210,150,420,150,210,150,	// 11-20
    240,150,180,150,150,300,330,420,300,180,	// 21-30
    120,30					// 31-32
};
 

//
// G_DoCompleted 
//
boolean		secretexit; 
extern char*	pagename; 
 
void G_ExitLevel (void) 
{ 
    secretexit = false; 
    gameaction = ga_completed; 
} 

// Here's for the german edition.
void G_SecretExitLevel (void) 
{ 
    // IF NO WOLF3D LEVELS, NO SECRET EXIT!
    if ( (gamemode == commercial)
      && (W_CheckNumForName("map31")<0))
	secretexit = false;
    else
	secretexit = true; 
    gameaction = ga_completed; 
} 
 
void G_DoCompleted (void) 
{ 
    int             i; 
	 
    gameaction = ga_nothing; 

    // Episode 0.5, the library (E0M1): there is no way out of it yet.
    if (gameepisode == 0)
	return;
 
    for (i=0 ; i<MAXPLAYERS ; i++) 
	if (playeringame[i]) 
	    G_PlayerFinishLevel (i);        // take away cards and stuff 
	 
    if (automapactive) 
	AM_Stop (); 
	
    if ( gamemode != commercial)
	switch(gamemap)
	{
	  case 8:
	    gameaction = ga_victory;
	    return;
	  case 9: 
	    for (i=0 ; i<MAXPLAYERS ; i++) 
		players[i].didsecret = true; 
	    break;
	}
		
//#if 0  Hmmm - why?
    if ( (gamemap == 8)
	 && (gamemode != commercial) ) 
    {
	// victory 
	gameaction = ga_victory; 
	return; 
    } 
	 
    if ( (gamemap == 9)
	 && (gamemode != commercial) ) 
    {
	// exit secret level 
	for (i=0 ; i<MAXPLAYERS ; i++) 
	    players[i].didsecret = true; 
    } 
//#endif
    
	 
    wminfo.didsecret = players[consoleplayer].didsecret; 
    wminfo.epsd = gameepisode -1; 
    wminfo.last = gamemap -1;
    
    // wminfo.next is 0 biased, unlike gamemap
    if ( gamemode == commercial)
    {
	if (secretexit)
	    switch(gamemap)
	    {
	      case 15: wminfo.next = 30; break;
	      case 31: wminfo.next = 31; break;
	    }
	else
	    switch(gamemap)
	    {
	      case 31:
	      case 32: wminfo.next = 15; break;
	      default: wminfo.next = gamemap;
	    }
    }
    else
    {
	if (secretexit) 
	    wminfo.next = 8; 	// go to secret level 
	else if (gamemap == 9) 
	{
	    // returning from secret level 
	    switch (gameepisode) 
	    { 
	      case 1: 
		wminfo.next = 3; 
		break; 
	      case 2: 
		wminfo.next = 5; 
		break; 
	      case 3: 
		wminfo.next = 6; 
		break; 
	      case 4:
		wminfo.next = 2;
		break;
	      case 5:	// SIGIL: E5M9 returns to E5M7
		wminfo.next = 6;
		break;
	    }                
	} 
	else 
	    wminfo.next = gamemap;          // go to next level 
    }
		 
    wminfo.maxkills = totalkills; 
    wminfo.maxitems = totalitems; 
    wminfo.maxsecret = totalsecret; 
    wminfo.maxfrags = 0; 
    if ( gamemode == commercial )
	wminfo.partime = 35*cpars[gamemap-1]; 
    else
	wminfo.partime = 35*pars[gameepisode][gamemap]; 
    wminfo.pnum = consoleplayer; 
 
    for (i=0 ; i<MAXPLAYERS ; i++) 
    { 
	wminfo.plyr[i].in = playeringame[i]; 
	wminfo.plyr[i].skills = players[i].killcount; 
	wminfo.plyr[i].sitems = players[i].itemcount; 
	wminfo.plyr[i].ssecret = players[i].secretcount; 
	wminfo.plyr[i].stime = leveltime; 
	memcpy (wminfo.plyr[i].frags, players[i].frags 
		, sizeof(wminfo.plyr[i].frags)); 
    } 
 
    gamestate = GS_INTERMISSION; 
    viewactive = false; 
    automapactive = false; 
 
    if (statcopy)
	memcpy (statcopy, &wminfo, sizeof(wminfo));
	
    WI_Start (&wminfo); 
} 


//
// G_WorldDone 
//
void G_WorldDone (void) 
{ 
    gameaction = ga_worlddone; 

    if (secretexit) 
	players[consoleplayer].didsecret = true; 

    if ( gamemode == commercial )
    {
	switch (gamemap)
	{
	  case 15:
	  case 31:
	    if (!secretexit)
		break;
	  case 6:
	  case 11:
	  case 20:
	  case 30:
	    F_StartFinale ();
	    break;
	}
    }
} 
 
void G_DoWorldDone (void) 
{        
    gamestate = GS_LEVEL; 
    gamemap = wminfo.next+1; 
    G_DoLoadLevel (); 
    gameaction = ga_nothing; 
    viewactive = true; 
} 
 


//
// G_InitFromSavegame
// Can be called by the startup code or the menu task. 
//
extern boolean setsizeneeded;
void R_ExecuteSetViewSize (void);

char	savename[256];

void G_LoadGame (char* name) 
{ 
    strcpy (savename, name); 
    gameaction = ga_loadgame; 
} 
 
#define VERSIONSIZE		16 


void G_DoLoadGame (void) 
{ 
    int		length; 
    int		i; 
    int		a,b,c; 
    char	vcheck[VERSIONSIZE]; 
	 
    gameaction = ga_nothing; 
	 
    length = M_ReadFile (savename, &savebuffer); 
    save_p = savebuffer + SAVESTRINGSIZE;
    
    // skip the description field 
    memset (vcheck,0,sizeof(vcheck)); 
    snprintf (vcheck, sizeof(vcheck), "native %i-%zu", VERSION, sizeof(void*) * CHAR_BIT); 
    if (length < SAVESTRINGSIZE + VERSIONSIZE + 10 ||
        memcmp (save_p, vcheck, strlen(vcheck) + 1))
    {
        Z_Free(savebuffer);
        players[consoleplayer].message = "INCOMPATIBLE SAVEGAME";
        return;
    }
    save_p += VERSIONSIZE; 
			 
    gameskill = *save_p++; 
    gameepisode = *save_p++; 
    gamemap = *save_p++; 
    for (i=0 ; i<MAXPLAYERS ; i++) 
	playeringame[i] = *save_p++; 

    // load a base level 
    G_InitNew (gameskill, gameepisode, gamemap); 
 
    // get the times 
    a = *save_p++; 
    b = *save_p++; 
    c = *save_p++; 
    leveltime = (a<<16) + (b<<8) + c; 
	 
    // dearchive all the modifications
    P_UnArchivePlayers (); 
    P_UnArchiveWorld (); 
    P_UnArchiveThinkers (); 
    P_UnArchiveSpecials (); 
    P_LibraryBufferEnd (savebuffer + length);
    P_UnArchiveLibrary ();
 
    if (*save_p != 0x1d) 
	I_Error ("Bad savegame");
    
    // done 
    Z_Free (savebuffer); 
 
    if (setsizeneeded)
	R_ExecuteSetViewSize ();
    
    // draw the pattern into the back screen
    R_FillBackScreen ();   
} 
 

//
// G_SaveGame
// Called by the menu task.
// Description is a 24 byte text string 
//
void
G_SaveGame
( int	slot,
  char*	description ) 
{ 
    savegameslot = slot; 
    strcpy (savedescription, description); 
    sendsave = true; 
} 
 
void G_DoSaveGame (void) 
{ 
    char	name[100]; 
    char	name2[VERSIONSIZE]; 
    char*	description; 
    int		length; 
    int		i; 
	
    if (M_CheckParm("-cdrom"))
	sprintf(name,"c:\\doomdata\\"SAVEGAMENAME"%d.dsg",savegameslot);
    else
	sprintf (name,SAVEGAMENAME"%d.dsg",savegameslot); 
    description = savedescription; 
	 
    size_t capacity = P_SaveGameBufferSize();
    save_p = savebuffer = calloc(1, capacity);
    if (!savebuffer) I_Error("Could not allocate savegame buffer");
	 
    memcpy (save_p, description, SAVESTRINGSIZE); 
    save_p += SAVESTRINGSIZE; 
    memset (name2, 0, sizeof(name2));
    snprintf (name2, sizeof(name2), "native %i-%zu", VERSION, sizeof(void*) * CHAR_BIT); 
    memcpy (save_p, name2, VERSIONSIZE); 
    save_p += VERSIONSIZE; 
	 
    *save_p++ = gameskill; 
    *save_p++ = gameepisode; 
    *save_p++ = gamemap; 
    for (i=0 ; i<MAXPLAYERS ; i++) 
	*save_p++ = playeringame[i]; 
    *save_p++ = leveltime>>16; 
    *save_p++ = leveltime>>8; 
    *save_p++ = leveltime; 
 
    P_ArchivePlayers (); 
    P_ArchiveWorld (); 
    P_ArchiveThinkers (); 
    P_ArchiveSpecials (); 
    P_LibraryBufferEnd (savebuffer + capacity);
    P_ArchiveLibrary ();
	 
    *save_p++ = 0x1d;		// consistancy marker 
	 
    length = save_p - savebuffer; 
    if ((size_t)length > capacity)
	I_Error ("Savegame buffer overrun"); 
    boolean written = M_WriteFile (name, savebuffer, length);
    free(savebuffer);
    savebuffer = NULL;
    gameaction = ga_nothing; 
    savedescription[0] = 0;		 
	 
    players[consoleplayer].message = written ? DEH_String (GGSAVED) : "COULD NOT SAVE GAME";

    // draw the pattern into the back screen
    R_FillBackScreen ();	
} 
 

//
// G_InitNew
// Can be called by the startup code or the menu task,
// consoleplayer, displayplayer, playeringame[] should be set. 
//
skill_t	d_skill; 
int     d_episode; 
int     d_map; 
 
void
G_DeferedInitNew
( skill_t	skill,
  int		episode,
  int		map) 
{ 
    d_skill = skill; 
    d_episode = episode; 
    d_map = map; 
    gameaction = ga_newgame; 
} 


static boolean G_MapExists (int episode, int map)
{
    char	name[9];

    if (gamemode == commercial && episode != 0)
	sprintf (name, "MAP%02d", map);
    else
	sprintf (name, "E%dM%d", episode, map);
    return W_CheckNumForName (name) >= 0;
}

boolean G_StepMap (int step, int* episode, int* map)
{
    int		e = *episode;
    int		m = *map;
    int		tries;

    for (tries = 0; tries < 40; tries++)
    {
	m += step;
	if (gamemode == commercial)
	{
	    if (m < 1) m = 32;
	    else if (m > 32) m = 1;
	}
	else
	{
	    if (m < 1) { m = 9; e--; }
	    else if (m > 9) { m = 1; e++; }
	    if (e < 1) e = 5;
	    else if (e > 5) e = 1;
	}
	if (G_MapExists (e, m))
	{
	    *episode = e;
	    *map = m;
	    return true;
	}
    }
    return false;
}

void G_DoNewGame (void) 
{
    demoplayback = false; 
    netdemo = false;
    netgame = false;
    deathmatch = false;
    playeringame[1] = playeringame[2] = playeringame[3] = 0;
    respawnparm = false;
    fastparm = false;
    nomonsters = false;
    consoleplayer = 0;
    G_InitNew (d_skill, d_episode, d_map); 
    gameaction = ga_nothing; 
} 

// The sky texture to be used instead of the F_SKY1 dummy.
extern  int	skytexture; 


void
G_InitNew
( skill_t	skill,
  int		episode,
  int		map ) 
{ 
    int             i; 
	 
    if (paused) 
    { 
	paused = false; 
	S_ResumeSound (); 
    } 
	

    if (skill > sk_nightmare) 
	skill = sk_nightmare;


    // This was quite messy with SPECIAL and commented parts.
    // Supposedly hacks to make the latest edition work.
    // It might not work properly.
    // Episode 0 is 0.5, the library (scp_7179_b): its one map, E0M1, in
    // any game that has it loaded.
    if (episode < 1 && !(episode == 0 && W_CheckNumForName ("E0M1") >= 0))
      episode = 1; 

    // Episode 5 is SIGIL's (E5M1-E5M9), when it is loaded on Ultimate DOOM.
    if ( gamemode == retail )
    {
      if (episode > 5 || (episode == 5 && W_CheckNumForName ("E5M1") < 0))
	episode = 4;
    }
    else if ( gamemode == shareware )
    {
      if (episode > 1) 
	   episode = 1;	// only start episode 1 on shareware
    }  
    else
    {
      if (episode > 3)
	episode = 3;
    }
    

  
    if (map < 1 || episode == 0) 
	map = 1;
    
    if ( (map > 9)
	 && ( gamemode != commercial) )
      map = 9; 
		 
    M_ClearRandom (); 
	 
    if (skill == sk_nightmare || respawnparm )
	respawnmonsters = true;
    else
	respawnmonsters = false;
		
    if (fastparm || (skill == sk_nightmare && gameskill != sk_nightmare) )
    { 
	for (i=S_SARG_RUN1 ; i<=S_SARG_PAIN2 ; i++) 
	    states[i].tics >>= 1; 
	mobjinfo[MT_BRUISERSHOT].speed = 20*FRACUNIT; 
	mobjinfo[MT_HEADSHOT].speed = 20*FRACUNIT; 
	mobjinfo[MT_TROOPSHOT].speed = 20*FRACUNIT; 
    } 
    else if (skill != sk_nightmare && gameskill == sk_nightmare) 
    { 
	for (i=S_SARG_RUN1 ; i<=S_SARG_PAIN2 ; i++) 
	    states[i].tics <<= 1; 
	mobjinfo[MT_BRUISERSHOT].speed = 15*FRACUNIT; 
	mobjinfo[MT_HEADSHOT].speed = 10*FRACUNIT; 
	mobjinfo[MT_TROOPSHOT].speed = 10*FRACUNIT; 
    } 
	 
			 
    // force players to be initialized upon first level load         
    for (i=0 ; i<MAXPLAYERS ; i++) 
	players[i].playerstate = PST_REBORN; 
 
    usergame = true;                // will be set false if a demo 
    paused = false; 
    demoplayback = false; 
    automapactive = false; 
    viewactive = true; 
    gameepisode = episode; 
    gamemap = map; 
    gameskill = skill; 
 
    viewactive = true;
    
    // set the sky map for the episode
    if ( gamemode == commercial)
    {
	skytexture = R_TextureNumForName ("SKY3");
	if (gamemap < 12)
	    skytexture = R_TextureNumForName ("SKY1");
	else
	    if (gamemap < 21)
		skytexture = R_TextureNumForName ("SKY2");
    }
    else
	switch (episode) 
	{ 
	  case 0:
	  case 1: 
	    skytexture = R_TextureNumForName ("SKY1"); 
	    break; 
	  case 2: 
	    skytexture = R_TextureNumForName ("SKY2"); 
	    break; 
	  case 3: 
	    skytexture = R_TextureNumForName ("SKY3"); 
	    break; 
	  case 4:	// Special Edition sky
	    skytexture = R_TextureNumForName ("SKY4");
	    break;
	  case 5:	// SIGIL's own, else the fourth episode's
	    skytexture = R_CheckTextureNumForName ("SKY5");
	    if (skytexture < 0)
		skytexture = R_TextureNumForName ("SKY4");
	    break;
	} 
 
    G_DoLoadLevel (); 
} 
 

//
// DEMO RECORDING 
// 
#define DEMOMARKER		0x80


void G_ReadDemoTiccmd (ticcmd_t* cmd) 
{ 
    if (*demo_p == DEMOMARKER) 
    {
	// end of demo data stream 
	G_CheckDemoStatus (); 
	return; 
    } 
    cmd->forwardmove = ((signed char)*demo_p++); 
    cmd->sidemove = ((signed char)*demo_p++); 
    cmd->angleturn = ((unsigned char)*demo_p++)<<8; 
    cmd->buttons = (unsigned char)*demo_p++; 
} 


void G_WriteDemoTiccmd (ticcmd_t* cmd) 
{ 
    if (gamekeydown['q'])           // press q to end demo recording 
	G_CheckDemoStatus (); 
    *demo_p++ = cmd->forwardmove; 
    *demo_p++ = cmd->sidemove; 
    *demo_p++ = (cmd->angleturn+128)>>8; 
    *demo_p++ = cmd->buttons; 
    demo_p -= 4; 
    if (demo_p > demoend - 16)
    {
	// no more space 
	G_CheckDemoStatus (); 
	return; 
    } 
	
    G_ReadDemoTiccmd (cmd);         // make SURE it is exactly the same 
} 
 
 
 
//
// G_RecordDemo 
// 
void G_RecordDemo (char* name) 
{ 
    int             i; 
    int				maxsize;
	
    usergame = false; 
    strcpy (demoname, name); 
    strcat (demoname, ".lmp"); 
    maxsize = 0x20000;
    i = M_CheckParm ("-maxdemo");
    if (i && i<myargc-1)
	maxsize = atoi(myargv[i+1])*1024;
    demobuffer = Z_Malloc (maxsize,PU_STATIC,NULL); 
    demoend = demobuffer + maxsize;
	
    demorecording = true; 
} 
 
 
void G_BeginRecording (void) 
{ 
    int             i; 
		
    demo_p = demobuffer;
	
    *demo_p++ = VERSION;
    *demo_p++ = gameskill; 
    *demo_p++ = gameepisode; 
    *demo_p++ = gamemap; 
    *demo_p++ = deathmatch; 
    *demo_p++ = respawnparm;
    *demo_p++ = fastparm;
    *demo_p++ = nomonsters;
    *demo_p++ = consoleplayer;
	 
    for (i=0 ; i<MAXPLAYERS ; i++) 
	*demo_p++ = playeringame[i]; 		 
} 
 

//
// G_PlayDemo 
//

char*	defdemoname; 
 
void G_DeferedPlayDemo (char* name) 
{ 
    defdemoname = name; 
    gameaction = ga_playdemo; 
} 
 
void G_DoPlayDemo (void) 
{ 
    skill_t skill; 
    int             i, episode, map; 
	 
    gameaction = ga_nothing; 
    demobuffer = demo_p = W_CacheLumpName (defdemoname, PU_STATIC); 
    int demo_version = *demo_p++;
    /* DOS 1.9 and the Linux release use the same demo command format. */
    if (demo_version != 109 && demo_version != VERSION)
    {
      fprintf( stderr, "Demo is from a different game version!\n");
      gameaction = ga_nothing;
      if (singledemo) I_Error("Unsupported demo version: %i", demo_version);
      D_AdvanceDemo();
      return;
    }
    
    skill = *demo_p++; 
    episode = *demo_p++; 
    map = *demo_p++; 
    deathmatch = *demo_p++;
    respawnparm = *demo_p++;
    fastparm = *demo_p++;
    nomonsters = *demo_p++;
    consoleplayer = *demo_p++;
	
    for (i=0 ; i<MAXPLAYERS ; i++) 
	playeringame[i] = *demo_p++; 
    if (playeringame[1]) 
    { 
	netgame = true; 
	netdemo = true; 
    }

    // don't spend a lot of time in loadlevel 
    precache = false;
    G_InitNew (skill, episode, map); 
    precache = true; 

    usergame = false; 
    demoplayback = true; 
} 

//
// G_TimeDemo 
//
void G_TimeDemo (char* name) 
{ 	 
    nodrawers = M_CheckParm ("-nodraw"); 
    noblit = M_CheckParm ("-noblit"); 
    timingdemo = true; 
    singletics = true; 

    defdemoname = name; 
    gameaction = ga_playdemo; 
} 
 
 
/* 
=================== 
= 
= G_CheckDemoStatus 
= 
= Called after a death or level completion to allow demos to be cleaned up 
= Returns true if a new demo loop action will take place 
=================== 
*/ 
 
boolean G_CheckDemoStatus (void) 
{ 
    int             endtime; 
	 
    if (timingdemo) 
    { 
	endtime = I_GetTime (); 
	I_Error ("timed %i gametics in %i realtics",gametic 
		 , endtime-starttime); 
    } 
	 
    if (demoplayback) 
    { 
	if (singledemo) 
	    I_Quit (); 
			 
	Z_ChangeTag (demobuffer, PU_CACHE); 
	demoplayback = false; 
	netdemo = false;
	netgame = false;
	deathmatch = false;
	playeringame[1] = playeringame[2] = playeringame[3] = 0;
	respawnparm = false;
	fastparm = false;
	nomonsters = false;
	consoleplayer = 0;
	D_AdvanceDemo (); 
	return true; 
    } 
 
    if (demorecording) 
    { 
	*demo_p++ = DEMOMARKER; 
	M_WriteFile (demoname, demobuffer, demo_p - demobuffer); 
	Z_Free (demobuffer); 
	demorecording = false; 
	I_Error ("Demo %s recorded",demoname); 
    } 
	 
    return false; 
} 
 
 
 
