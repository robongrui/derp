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
//
// DESCRIPTION:
//	System interface, sound.
//
//-----------------------------------------------------------------------------

#ifndef __I_SOUND__
#define __I_SOUND__

#include "doomdef.h"

// UNIX hack, to be removed.
#ifdef SNDSERV
#include <stdio.h>
extern FILE* sndserver;
extern char* sndserver_filename;
#endif

#include "doomstat.h"
#include "sounds.h"



// Init at program start...
void I_InitSound();

// ... update sound buffer and audio device at runtime...
void I_UpdateSound(void);
void I_SubmitSound(void);

// ... shut down and relase at program termination.
void I_ShutdownSound(void);


//
//  SFX I/O
//

// Initialize channels?
void I_SetChannels();

// Get raw data lump index for sound descriptor.
int I_GetSfxLumpNum (sfxinfo_t* sfxinfo );


// Starts a sound in a particular sound channel.
int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority );


// Stops a sound channel.
void I_StopSound(int handle);

// A DMX sound lump by name, whole and dry, from no place; 0 if there is none.
int I_SoundLump(const char* name);
// A DMX sound lump played once at a map point, as any monster's sound is heard
// there: distance, walls, the room. loud in percent. How many tics it lasts, 0
// if it did not play (DERP's soldiers' shouts, p_barks.h).
int I_BarkLump(const char* name, fixed_t x, fixed_t y, fixed_t z, int loud, int pitch);
// The next shout is meant to be made out: however it is walled off, it keeps its words.
void I_BarkClear(void);
// The last shout's handle for I_StopSound (0: it was not played).
int I_BarkVoice(void);

// Called by S_*() functions
//  to see if a channel is still playing.
// Returns 0 if no longer playing, 1 if playing.
int I_SoundIsPlaying(int handle);

// Updates the volume, separation,
//  and pitch of a sound channel.
void
I_UpdateSoundParams
( int		handle,
  int		vol,
  int		sep,
  int		pitch );

// S_UpdateSounds' pass over the playing sounds, between these: a mixer may
// take what is asked of each sound in at the end at once (and do a new
// level's sound preparation at the begin of the first pass in it).
void I_BeginSoundUpdate(void);
void I_EndSoundUpdate(void);

// Where a positioned sound is in the map, for muffling and distance.
// Handle 0 places the sound the next I_StartSound starts.
void I_SoundAt(int handle, fixed_t x, fixed_t y, fixed_t z);
// The next I_StartSound is the listener's own (a mixer may hear it through
// a suit when there is no air around them).
void I_SoundOwn(void);
// How far an effect carries, FRACUNIT as DOOM hears it (gone past 1200
// units): a mixer with a natural falloff lets loud ones (gunfire, blasts)
// reach farther, fainter and muffled.
fixed_t I_SoundReach(int id);
/* Authored maps: sound carries past DOOM's 1200 units (audio/level_sound.h).
   A sound too far for a channel goes to I_FarSound, heard from where it was
   made, at volume (DOOM's 0-15, before distance). */
int I_LevelSound(void);
void I_FarSound(int id, fixed_t x, fixed_t y, fixed_t z, int volume);
/* The level's map is about to be freed: stops whatever still reads it. */
void I_LevelEnding(void);


//
//  MUSIC I/O
//
void I_InitMusic(void);
void I_ShutdownMusic(void);
// Volume.
void I_SetMusicVolume(int volume);
// PAUSE game handling.
void I_PauseSong(int handle);
void I_ResumeSong(int handle);
// Registers a song handle to song data.
int I_RegisterSong(void *data);
// Called by anything that wishes to start music.
//  plays a song, and when the song is done,
//  starts playing it again in an endless loop.
// Horrible thing to do, considering.
void
I_PlaySong
( int		handle,
  int		looping );
// Stops a song over 3 seconds.
void I_StopSong(int handle);
// See above (register), then think backwards
void I_UnRegisterSong(int handle);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
