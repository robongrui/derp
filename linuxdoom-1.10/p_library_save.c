//
// Savegames: the library's own state, after the specials, on the library
// only (see p_library.h). Each part writes its own, in this order: the floor
// (p_library.c), the shaft, the people, the day, the books opened, the reader, the fallers, the kiosk. The people are their mobjs
// by their place among the saved mobjs: P_ArchiveThinkers writes them in
// thinker order, and loading adds them back in the same order.
//

#include <limits.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_system.h"
#include "p_saveg.h"
#include "p_library_local.h"

#define SAVE_MARK	"LIBA"

// The level's clock (leveltime), kept whole here: DOOM's own save keeps
// three bytes of it, 5.5 days, and the library is one level for the whole
// game. A clock loaded past REBASE is renumbered back by whole drawing
// periods (scene3d's clockWrapTics, so nothing drawn jumps), the library's
// moments with it, and what was taken off is added to the time spent here
// (ticsbefore, 64 bits), so an edited or a very long game never brings the
// running clock near its 32-bit end. Only a load renumbers: it builds the
// level afresh, the renderer's and the sound's moments with it.
#define REBASE		(1 << 30)		// tics: about 354 days
#define PERIOD		(TICRATE*65536)		// the drawing clock's period

static long long	ticsbefore;	// tics spent here before the clock's last renumbering
static byte*		save_end;	// where the save being written or loaded ends


void L_Put (const void* data, size_t size)
{
    if (save_end && (save_p > save_end || size > (size_t)(save_end - save_p)))
	I_Error ("Library: the save outgrew its buffer");
    memcpy (save_p, data, size);
    save_p += size;
}


// What a save gives, never past its end: a save cut short or made up
// stops loading with a message, it doesn't read on into memory.
void L_Get (void* data, size_t size)
{
    if (save_end && (save_p > save_end || size > (size_t)(save_end - save_p)))
	I_Error ("Bad savegame: the library's state is cut short");
    memcpy (data, save_p, size);
    save_p += size;
}


void P_LibraryBufferEnd (byte* end)
{
    save_end = end;
}


long long P_LibraryTicsHere (void)
{
    return ticsbefore + leveltime;
}


void L_NewTime (void)
{
    ticsbefore = 0;
}


int L_MobjOrdinal (mobj_t* mo)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (L_IsMobj (th))
	{
	    if ((mobj_t*)th == mo)
		return n;
	    n++;
	}
    return -1;
}


mobj_t* L_NthMobj (int ordinal)
{
    thinker_t*	th;
    int		n = 0;

    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (L_IsMobj (th) && n++ == ordinal)
	    return (mobj_t*)th;
    return NULL;
}


size_t P_LibrarySaveSize (void)
{
    if (!P_Library ())
	return 0;
    return 1024 + L_FloorSaveSize () + L_PeopleSaveSize () + L_BooksSaveSize ()
	+ L_ReaderSaveSize () + L_FallersSaveSize ();
}


void P_ArchiveLibrary (void)
{
    if (!P_Library ())
	return;
    L_Put (SAVE_MARK, 4);
    L_Put (&leveltime, sizeof leveltime);
    L_Put (&ticsbefore, sizeof ticsbefore);
    L_ArchiveFloor ();
    L_ArchiveShaft ();
    L_ArchivePeople ();
    L_ArchiveDay ();
    L_ArchiveBooks ();
    L_ArchiveReader ();
    L_ArchiveFallers ();
    L_ArchiveKiosk ();
    save_end = NULL;
}


// After P_UnArchiveThinkers: the level was set up afresh for the seed on
// the command line, so all of it is put back as it was saved.
void P_UnArchiveLibrary (void)
{
    char	mark[4];
    int		clock;
    long long	before;

    if (!P_Library ())
	return;
    L_Get (mark, sizeof mark);
    if (memcmp (mark, SAVE_MARK, 4))
	I_Error ("Bad savegame: the library's state is missing");
    L_Get (&clock, sizeof clock);
    L_Get (&before, sizeof before);
    if (clock < 0 || before < 0 || before > LLONG_MAX - INT_MAX)
	I_Error ("Bad savegame: the library's clock");
    leveltime = clock;
    ticsbefore = before;
    L_UnArchiveFloor ();
    L_UnArchiveShaft ();
    L_UnArchivePeople ();
    L_UnArchiveDay ();
    L_UnArchiveBooks ();
    L_UnArchiveReader ();
    L_UnArchiveFallers ();
    L_UnArchiveKiosk ();
    if (leveltime >= REBASE)
    {
	int	off = leveltime - leveltime % PERIOD;

	leveltime -= off;
	ticsbefore += off;
	L_RebasePeople (off);
	L_RebaseDay (off);
	L_RebaseKiosk (off);
    }
    save_end = NULL;
    L_Log ("loaded", players[consoleplayer].mo);
}
