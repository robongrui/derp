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
// DESCRIPTION:
//	Map Objects, MObj, definition and handling.
//
//-----------------------------------------------------------------------------


#ifndef __P_MOBJ__
#define __P_MOBJ__

// Basics.
#include "tables.h"
#include "m_fixed.h"

// We need the thinker_t stuff.
#include "d_think.h"

// We need the WAD data structure for Map things,
// from the THINGS lump.
#include "doomdata.h"

// States are tied to finite states are
//  tied to animation frames.
// Needs precompiled tables/data structures.
#include "info.h"



#ifdef __GNUG__
#pragma interface
#endif



//
// NOTES: mobj_t
//
// mobj_ts are used to tell the refresh where to draw an image,
// tell the world simulation when objects are contacted,
// and tell the sound driver how to position a sound.
//
// The refresh uses the next and prev links to follow
// lists of things in sectors as they are being drawn.
// The sprite, frame, and angle elements determine which patch_t
// is used to draw the sprite if it is visible.
// The sprite and frame values are allmost allways set
// from state_t structures.
// The statescr.exe utility generates the states.h and states.c
// files that contain the sprite/frame numbers from the
// statescr.txt source file.
// The xyz origin point represents a point at the bottom middle
// of the sprite (between the feet of a biped).
// This is the default origin position for patch_ts grabbed
// with lumpy.exe.
// A walking creature will have its z equal to the floor
// it is standing on.
//
// The sound code uses the x,y, and subsector fields
// to do stereo positioning of any sound effited by the mobj_t.
//
// The play simulation uses the blocklinks, x,y,z, radius, height
// to determine when mobj_ts are touching each other,
// touching lines in the map, or hit by trace lines (gunshots,
// lines of sight, etc).
// The mobj_t->flags element has various bit flags
// used by the simulation.
//
// Every mobj_t is linked into a single sector
// based on its origin coordinates.
// The subsector_t is found with R_PointInSubsector(x,y),
// and the sector_t can be found with subsector->sector.
// The sector links are only used by the rendering code,
// the play simulation does not care about them at all.
//
// Any mobj_t that needs to be acted upon by something else
// in the play world (block movement, be shot, etc) will also
// need to be linked into the blockmap.
// If the thing has the MF_NOBLOCK flag set, it will not use
// the block links. It can still interact with other things,
// but only as the instigator (missiles will run into other
// things, but nothing can run into a missile).
// Each block in the grid is 128*128 units, and knows about
// every line_t that it contains a piece of, and every
// interactable mobj_t that has its origin contained.  
//
// A valid mobj_t is a mobj_t that has the proper subsector_t
// filled in for its xy coordinates and is linked into the
// sector from which the subsector was made, or has the
// MF_NOSECTOR flag set (the subsector_t needs to be valid
// even if MF_NOSECTOR is set), and is linked into a blockmap
// block or has the MF_NOBLOCKMAP flag set.
// Links should only be modified by the P_[Un]SetThingPosition()
// functions.
// Do not change the MF_NO? flags while a thing is valid.
//
// Any questions?
//

//
// Misc. mobj flags
//
typedef enum
{
    // Call P_SpecialThing when touched.
    MF_SPECIAL		= 1,
    // Blocks.
    MF_SOLID		= 2,
    // Can be hit.
    MF_SHOOTABLE	= 4,
    // Don't use the sector links (invisible but touchable).
    MF_NOSECTOR		= 8,
    // Don't use the blocklinks (inert but displayable)
    MF_NOBLOCKMAP	= 16,                    

    // Not to be activated by sound, deaf monster.
    MF_AMBUSH		= 32,
    // Will try to attack right back.
    MF_JUSTHIT		= 64,
    // Will take at least one step before attacking.
    MF_JUSTATTACKED	= 128,
    // On level spawning (initial position),
    //  hang from ceiling instead of stand on floor.
    MF_SPAWNCEILING	= 256,
    // Don't apply gravity (every tic),
    //  that is, object will float, keeping current height
    //  or changing it actively.
    MF_NOGRAVITY	= 512,

    // Movement flags.
    // This allows jumps from high places.
    MF_DROPOFF		= 0x400,
    // For players, will pick up items.
    MF_PICKUP		= 0x800,
    // Player cheat. ???
    MF_NOCLIP		= 0x1000,
    // Player: keep info about sliding along walls.
    MF_SLIDE		= 0x2000,
    // Allow moves to any height, no gravity.
    // For active floaters, e.g. cacodemons, pain elementals.
    MF_FLOAT		= 0x4000,
    // Don't cross lines
    //   ??? or look at heights on teleport.
    MF_TELEPORT		= 0x8000,
    // Don't hit same species, explode on block.
    // Player missiles as well as fireballs of various kinds.
    MF_MISSILE		= 0x10000,	
    // Dropped by a demon, not level spawned.
    // E.g. ammo clips dropped by dying former humans.
    MF_DROPPED		= 0x20000,
    // Use fuzzy draw (shadow demons or spectres),
    //  temporary player invisibility powerup.
    MF_SHADOW		= 0x40000,
    // Flag: don't bleed when shot (use puff),
    //  barrels and shootable furniture shall not bleed.
    MF_NOBLOOD		= 0x80000,
    // Don't stop moving halfway off a step,
    //  that is, have dead bodies slide down all the way.
    MF_CORPSE		= 0x100000,
    // Floating to a height for a move, ???
    //  don't auto float to target's height.
    MF_INFLOAT		= 0x200000,

    // On kill, count this enemy object
    //  towards intermission kill total.
    // Happy gathering.
    MF_COUNTKILL	= 0x400000,
    
    // On picking up, count this item object
    //  towards intermission item total.
    MF_COUNTITEM	= 0x800000,

    // Special handling: skull in flight.
    // Neither a cacodemon nor a missile.
    MF_SKULLFLY		= 0x1000000,

    // Don't spawn this object
    //  in death match mode (e.g. key cards).
    MF_NOTDMATCH    	= 0x2000000,

    // Player sprites in multiplayer modes are modified
    //  using an internal color lookup table for re-indexing.
    // If 0x4 0x8 or 0xc,
    //  use a translation table for player colormaps
    MF_TRANSLATION  	= 0xc000000,
    // Hmm ???.
    MF_TRANSSHIFT	= 26

} mobjflag_t;


// Map Object definition.
typedef struct mobj_s
{
    // List: thinker links.
    thinker_t		thinker;

    // Info for drawing: position.
    fixed_t		x;
    fixed_t		y;
    fixed_t		z;

    // More list: links in sector (if needed)
    struct mobj_s*	snext;
    struct mobj_s*	sprev;

    //More drawing info: to determine current sprite.
    angle_t		angle;	// orientation
    spritenum_t		sprite;	// used to find patch_t and flip value
    int			frame;	// might be ORed with FF_FULLBRIGHT

    // Interaction info, by BLOCKMAP.
    // Links in blocks (if needed).
    struct mobj_s*	bnext;
    struct mobj_s*	bprev;
    
    struct subsector_s*	subsector;

    // The closest interval over all contacted Sectors.
    fixed_t		floorz;
    fixed_t		ceilingz;

    // For movement checking.
    fixed_t		radius;
    fixed_t		height;	

    // Momentums, used to update position.
    fixed_t		momx;
    fixed_t		momy;
    fixed_t		momz;

    // If == validcount, already checked.
    int			validcount;

    mobjtype_t		type;
    mobjinfo_t*		info;	// &mobjinfo[mobj->type]
    
    int			tics;	// state tic counter
    state_t*		state;
    int			flags;
    int			health;

    // Movement direction, movement generation (zig-zagging).
    int			movedir;	// 0-7
    int			movecount;	// when 0, select a new dir

    // Thing being chased/attacked (or NULL),
    // also the originator for missiles.
    struct mobj_s*	target;

    // Reaction time: if non 0, don't attack yet.
    // Used by player to freeze a bit after teleporting.
    int			reactiontime;   

    // If >0, the target will be chased
    // no matter what (even if shot)
    int			threshold;

    // Additional info record for player avatars only.
    // Only valid if type == MT_PLAYER
    struct player_s*	player;

    // Player number last looked for.
    int			lastlook;	

    // For nightmare respawn.
    mapthing_t		spawnpoint;	

    // Thing being chased/attacked for tracers.
    struct mobj_s*	tracer;	
    
    // DERP's monsters (p_derp.h): a number no other thing in the level
    // has had (a freed thing's memory is soon another's, so a pointer kept
    // would not say who it was), the sector it was placed in (its home),
    // who last threw it and when (a fall or a floor kills it for them),
    // and the damage it took in the tic hurttic.
    int			serial;
    int			homesector;
    int			shover;
    int			shovetic;
    int			hurttic;
    int			hurt;

    // What it knows and means to do (p_senses.h): its state of mind, how
    // near it is to spotting the player (0-1000) and when it last looked,
    // where it thinks the player is and since when, where it is going and
    // since when, when its search began, and the next point of its route.
    int			mind;
    int			awareness;
    int			looktic;
    fixed_t		knownx;
    fixed_t		knowny;
    int			knowntic;
    fixed_t		goalx;
    fixed_t		goaly;
    int			goaltic;
    int			searchtic;
    fixed_t		wayx;
    fixed_t		wayy;
    int			waytic;
    // When a walk last got nearer its waypoint, and the nearest it got
    // (no progress a while: it stands; longer, it tries any way).
    int			movedtic;
    fixed_t		waydist;

    // Its squad and nerve (p_tactics.h): the squad (its home room, plus
    // one), nerve 0-100, its own heard dying, how alert (0-2), when its head
    // was last hit and whether it died without a sound; the tactic it is
    // at (p_mind.h), a step of it and since when, the place of its tactic
    // (coverx: its cover, its ambush post, its hiding place, where the squad
    // it joins is, where it believes him on a flank) and the spot it leans out
    // to from cover (peekx), which way it faces in ambush, the squad it goes
    // to join, when it thinks again and until when it kneels.
    int			squad;
    int			nerve;
    int			losses;
    int			alert;
    int			headtic;
    int			silent;
    int			tactic;
    int			tactictic;
    fixed_t		coverx;
    fixed_t		covery;
    fixed_t		peekx;
    fixed_t		peeky;
    angle_t		facing;
    int			joinsquad;
    int			decidetic;
    int			kneeltic;
    // Its cover is a low wall: it kneels behind it and stands to fire.
    int			lowcover;
    // Its ambush is a cover: sprung, it fights from there (p_coverplan.h).
    int			fightsfrom;
    int			heldready;	// waiting where it stood, no post to go to
    // Fellow soldiers heard dying (any squad's) and how many of them its
    // last ambush was for, its health when it last retreated, when it last
    // fired, when its alert was last raised.
    int			fallen;
    int			ambushed;
    // Its fire position in a fight (a place of its own in range of him; on a
    // bound, where the bound goes) and when it was last looked for.
    fixed_t		firex;
    fixed_t		firey;
    int			firepostic;
    // An opening its way keeps off: the held doorway a soldier goes round
    // (STEP_ROUNDING) or an imp flanks (STEP_ROUND_CHOKE).
    fixed_t		avoidx;
    fixed_t		avoidy;
    // When it last set off on a bound (its team covering it).
    int			boundtic;
    // Until when it holds the way out behind him (-1: on its way there).
    int			holdtic;
    // When it began firing on the move, 0 if it is not (p_gunwalk.h).
    int			gunwalk;
    // When it was first found standing in a narrow doorway, wanting to fire.
    int			framedtic;
    // How much of its lean out of cover shows, of 256 (P_DerpLean).
    int			leanshow;
    // Until when an imp shot on its way round him fights instead (p_demons.c).
    int			answertic;
    // Whether it has a way to him on foot, and when that was last looked at.
    int			noway;
    int			waychecktic;
    // An imp going round him: where it was when, to see that it is getting anywhere.
    int			stalltic;
    fixed_t		stallx, stally;
    int			stalls;
    // Clearing its line of fire: steps taken, which way next, holding until when.
    int			asidesteps;
    int			asideway;
    int			asidetic;
    // An imp lying in wait (p_hunters.h): what it is doing, since when, where.
    int			lurk;
    int			lurktic;
    int			lurkseen;	// since when he has been looking at it
    int			lurkslip;	// when it last found its place about to be seen
    int			sighttic;	// when it last had him in sight
    int			beattic;	// no first shot before this (P_DerpSighted)
    int			nowaytic;	// when it last found it could not get somewhere
    fixed_t		nowayx, nowayy;	// where that was
    fixed_t		flankx, flanky;	// its way keeps clear of here (p_pincer.c), by flankr
    int			flankr;
    int			flankwait;	// since when it has waited for its partner
    int			flankleft;	// cells of its way still to go
    int			flanktic;	// when it last looked how far
    int			nearestonly;	// no way there: only as near as it gets
    int			wordtic;	// told one of its own went off to look
    int			wordrang;	// when word of a fight went round to it
    int			wordgo;		// when that word gets to it
    int			wordleft;	// when it told them it was going
    fixed_t		wordx;		// where the fight is, as told
    fixed_t		wordy;
    int			barkvoice;	// what it is saying, as the sound knows it
    int			barkuntil;	// and until when
    int			pincer;		// the serial of the imp coming in the other way
    fixed_t		lurkx, lurky;
    // When it last took a chase step: tactics get a turn between shots.
    int			chasetic;
    // When a step of its was refused for a floor that hurts (P_DerpKeepsOut).
    int			drytic;
    // A pinky's charge (p_charge.h): when it began (0: none), which way it is
    // going, and when it may have another.
    int			chargetic;
    angle_t		chargeangle;
    int			chargeagain;
    // Shots as information (p_fire.h): how suppressed it was when a round
    // last went by and when, when it last heard one arrive, until when it
    // lays fire on where he was, and how far into a burst it is.
    int			suppress;
    int			suppresstic;
    int			roundtic;
    int			layfire;
    int			burst;
    int			bursttic;
    // When it last refused a choke (p_portals.h).
    int			chokedtic;
    // How far into a run it is (P_DerpSprint).
    int			gait;
    // The way it is drawn facing, turning after angle (P_DerpDrawAngle).
    angle_t		faceangle;
    int			faceset;
    // A climber's jump: until when it gathers itself, and when it last landed.
    int			leapwind;
    int			landtic;

    // A climber's jump on its way (p_nav.h's navclimb): whether one is
    // ahead, its take-off and landing (and the landing's floor), and when it
    // last leapt.
    int			leapready;
    fixed_t		leapx;
    fixed_t		leapy;
    fixed_t		landx;
    fixed_t		landy;
    fixed_t		landz;
    fixed_t		leapsill;	// the height it must clear (a ledge, a sill)
    int			leaptic;
    // When it last blinked (a short teleport).
    int			blinktic;
    // A climber's jump under way (p_senses.c's Leap, P_DerpJumped): the
    // cells it jumps from and to; and the last jump that did not land
    // there, how often it failed, and when.
    int			jumping;
    int			jumpfrom;
    int			jumpto;
    int			badfrom;
    int			badto;
    int			badfails;
    int			badtic;
    // When an imp set off round him (p_demons.c's Encircle; 0 none).
    int			plantic;
    // The key it guards (the key's serial, 0 none) and where (p_tactics.c).
    int			guard;
    fixed_t		guardx;
    fixed_t		guardy;
    int			retreathealth;
    int			firetic;
    int			alerttic;

    // A demon's (p_demons.h): when the player last saw or hurt it (it heals
    // only some while after) and how much it has healed this level, the
    // swipes of its flurry so far, its pounce
    // (0 none, 1 crouched, 2 in the air, 3 struck) and when it last began or
    // ended one, when its claws last landed, and until when pain makes it
    // quicker.
    int			seentic;
    int			healed;
    int			combo;
    int			pounce;
    int			vault;		// that pounce goes over him: no blow, and through him
    int			rushtic;	// its last rush at him began then (0: not rushing)
    int			rushed;		// and it has had it: no running away after that
    int			hang;		// on the ceiling, going up or coming down (p_hang.c)
    int			hangtic;	// since when
    int			pouncetic;
    int			striketic;
    int			angrytic;

    // Its feet swept away (p_swept.h): since when (0: not), and the way they went.
    int			swepttic;
    fixed_t		sweptx, swepty;

    // Its steps walked out (p_strides.h): where this chase step's steps
    // went (the first where it began) and how many points, the tic they
    // were taken, and over how many tics it walks them (0: not walking).
    fixed_t		stridex[5];
    fixed_t		stridey[5];
    int			strides;
    int			stridetic;
    int			stridetics;

} mobj_t;



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
