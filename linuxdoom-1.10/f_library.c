//
// The end of the library (scp_7179_b/README.md): he has found his book. The
// screen goes black; YOU CAN LEAVE NOW; black; the credits (he is free,
// and where the library comes from); black again; a voice says "hate"
// (DSLIBEND, in the library's WAD when it has one) and the word stays,
// and then nothing, for as long as the game is left running. It is a
// finale of its own: no skipping it, no level after it. (See f_finale.c.)
//

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "i_sound.h"
#include "v_video.h"
#include "f_library.h"

void M_WriteText (int x, int y, char* string);
int M_StringWidth (char* string);

#define BLACK		(2*TICRATE)	// the first dark
#define LEAVE		(5*TICRATE)	// YOU CAN LEAVE NOW
#define BETWEEN		(2*TICRATE)	// dark, before the credits
#define CREDITS		(8*TICRATE)	// the credits
#define AGAIN		(2*TICRATE)	// dark again, before the voice
#define VOICE		(BLACK + LEAVE + BETWEEN + CREDITS + AGAIN)

static char* const credits[] =
{
    "CONGRATULATIONS.", "", "YOU ARE FREE.", "", "",
    "INSPIRED BY A SHORT STAY IN HELL", "BY STEVEN L. PECK", "",
    "AND THE LIBRARY OF BABEL", "BY JORGE LUIS BORGES", "", "",
    "THANK YOU FOR PLAYING.", NULL,
};

static boolean		ending;
static int		count;


void F_StartLibraryEnding (void)
{
    gameaction = ga_nothing;
    gamestate = GS_FINALE;
    viewactive = false;
    automapactive = false;
    ending = true;
    count = 0;
}


void F_StopLibraryEnding (void)
{
    ending = false;
}


boolean F_LibraryTicker (void)
{
    if (!ending)
	return false;
    if (count == VOICE)
	I_SoundLump ("DSLIBEND");
    if (count <= VOICE)
	count++;
    return true;
}


static void Centered (int y, char* text)
{
    M_WriteText (160 - M_StringWidth (text)/2, y, text);
}


boolean F_LibraryDrawer (void)
{
    int		i, from = BLACK + LEAVE + BETWEEN;

    if (!ending)
	return false;
    memset (screens[0], 0, SCREENWIDTH*SCREENHEIGHT);	// PLAYPAL's first colour: black
    if (count >= BLACK && count < BLACK + LEAVE)
	Centered (96, "YOU CAN LEAVE NOW");
    else if (count >= from && count < from + CREDITS)
	for (i = 0; credits[i]; i++)
	    Centered (30 + i*11, credits[i]);
    else if (count > VOICE)
	Centered (96, "HATE");
    V_MarkRect (0, 0, SCREENWIDTH, SCREENHEIGHT);
    return true;
}
