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
//	Refresh/render internal state variables (global).
//
//-----------------------------------------------------------------------------


#ifndef __R_STATE__
#define __R_STATE__

// Need data structure definitions.
#include "d_player.h"
#include "r_data.h"



#ifdef __GNUG__
#pragma interface
#endif



//
// Refresh internal data structures,
//  for rendering.
//

// needed for texture pegging
extern fixed_t*		textureheight;

// needed for pre rendering (fracs)
extern fixed_t*		spritewidth;

extern fixed_t*		spriteoffset;
extern fixed_t*		spritetopoffset;

extern lighttable_t*	colormaps;

extern int		viewwidth;
extern int		scaledviewwidth;
extern int		viewheight;

extern int		firstflat;

// for global animation
extern int*		flattranslation;	
extern int*		texturetranslation;	


// Sprite....
extern int		firstspritelump;
extern int		lastspritelump;
extern int		numspritelumps;



//
// Lookup tables for map data.
//
extern int		numsprites;
extern spritedef_t*	sprites;

extern int		numvertexes;
extern vertex_t*	vertexes;

extern int		numsegs;
extern seg_t*		segs;

extern int		numsectors;
extern sector_t*	sectors;

extern int		numsubsectors;
extern subsector_t*	subsectors;

extern int		numnodes;
extern node_t*		nodes;

extern int		numlines;
extern line_t*		lines;

extern int		numsides;
extern side_t*		sides;


//
// POV data.
//
/* Optional native renderer hooks. The classic/headless builds leave these null. */
extern void (*R_AcceleratedView)(player_t *player);
extern void (*R_BeforeTic)(void);
// Renderer-only shot notification; projectile is a mobj type or -1 for gunfire.
extern void (*R_MuzzleFlash)(mobj_t *source, int weapon, int projectile);
// Renderer-only hitscan wall impact: side faces the shooter; x,y,z is the puff.
extern void (*R_WallImpact)(line_t *line, int side, fixed_t x, fixed_t y, fixed_t z);
// Renderer-only hitscan blood: x,y,z is the blood spot, dx,dy the shot direction,
// slope its vertical rise per horizontal unit (to locate the hit on the sprite).
extern void (*R_BloodHit)(mobj_t *target, mobj_t *shooter, fixed_t x, fixed_t y, fixed_t z, fixed_t dx, fixed_t dy, fixed_t slope, int damage);
// Renderer only: the game's airborne blood just spawned, and whose it is.
extern void (*R_BloodSpilled)(mobj_t *blood, mobj_t *bleeder);
// Every damage event, before it is applied (renderer only: who hit what, with what).
extern void (*R_DamageHit)(mobj_t *target, mobj_t *inflictor, mobj_t *source, int damage);
// Renderer-only hitscan floor or ceiling impact (la_freeaim shots); true when
// the renderer shows it as a splash, so no puff is spawned.
extern boolean (*R_PlaneImpact)(sector_t *sector, boolean ceiling, fixed_t x, fixed_t y, fixed_t z);
// Renderer-only: a hitscan shot passes through a two-sided line's opening at
// x,y,z, travelling along dx,dy (window glass breaks).
extern void (*R_ShotPassed)(line_t *line, fixed_t x, fixed_t y, fixed_t z, fixed_t dx, fixed_t dy);
// A body struck the glass on line hard enough to break it (Phobos, P_Strike): it shatters at
// x, y, z, the body going dx, dy.
extern void (*R_GlassStruck)(line_t *line, fixed_t x, fixed_t y, fixed_t z, fixed_t dx, fixed_t dy);
// DERP: whether weather blowing in at a broken window reaches mo (the
// renderer knows the panes and the wind); NULL: only the open sky does.
extern int (*R_RainedIn)(mobj_t *mo);
extern boolean r_uncapped;
extern unsigned r_levelserial;
extern fixed_t		viewx;
extern fixed_t		viewy;
extern fixed_t		viewz;

extern angle_t		viewangle;
extern player_t*	viewplayer;


// ?
extern angle_t		clipangle;

extern int		viewangletox[FINEANGLES/2];
extern angle_t		xtoviewangle[SCREENWIDTH+1];
//extern fixed_t		finetangent[FINEANGLES/2];

extern fixed_t		rw_distance;
extern angle_t		rw_normalangle;



// angle to line origin
extern int		rw_angle1;

// Segs count?
extern int		sscount;

extern visplane_t*	floorplane;
extern visplane_t*	ceilingplane;


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
