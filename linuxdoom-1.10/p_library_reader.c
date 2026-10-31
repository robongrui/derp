//
// The reader (scp_7179_b/README.md, Your book): who he says he is, asked
// when he first stands in the library (m_library.c) and kept in his
// savegames, and whether a book he opens is his. His book is the biography
// of his answers whose wording number v has v^65537 mod N equal to his
// target, a hash of the answers and the seed; N is RSA-2048, so the book
// exists, exactly once (if 65537 shares no factor with lambda(N), which only
// N's unknown factors could show: false for about 2 in 65,536 such N), and
// can't be worked out without them. Finding it ends the game
// (f_library.c). (See p_library.h, p_library_local.h.)
//

#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "f_library.h"
#include "p_library_local.h"

static library_reader	reader;
static boolean		known;		// he has answered
static boolean		found;		// and found his book
static library_modulus	modulus;
static const char*	modulusof;	// the modulus set up, in decimal
static const char*	wanted;		// another than RSA-2048 (P_LibraryModulus), or NULL
static library_number	target;
static char*		text;		// a book being checked, in letters


static void Aim (void)
{
    char	identity[LIBRARY_IDENTITY_SIZE];
    const char*	n = wanted ? wanted : Library_RSA2048;

    if (n != modulusof)
    {
	Library_Modulus (&modulus, n);
	modulusof = n;
    }
    Library_Identity (&reader, identity);
    Library_Target (&modulus, identity, L_Seed (), &target);
}


void P_LibraryModulus (const char* decimal)
{
    wanted = decimal;
    if (known)
	Aim ();
}


boolean P_LibrarySetReader (const char* answers[LIBRARY_FACTS])
{
    library_reader	answered;
    int			f;

    for (f = 0; f < LIBRARY_FACTS; f++)
	if (!Library_Answer (f, answers[f], answered.fact[f]))
	    return false;
    reader = answered;
    known = true;
    Aim ();
    L_Log ("answered", players[consoleplayer].mo);
    return true;
}


boolean P_LibraryAnswerFits (int fact, const char* answer)
{
    char	out[LIBRARY_FACT_SIZE];

    return Library_Answer (fact, answer, out) != 0;
}


boolean P_LibraryNeedsReader (void)
{
    return P_Library () && !known && gamestate == GS_LEVEL && usergame
	&& !demoplayback && !netgame;
}


// -libraryreader "name/year/birthplace/hometown/sign/pet/food": answered
// already, as a recorded game must be.
void L_ReaderLevel (void)
{
    const char*	answers[LIBRARY_FACTS];
    char	given[LIBRARY_FACTS*LIBRARY_FACT_SIZE];
    char*	part;
    int		p = M_CheckParm ("-libraryreader"), f;

    known = found = false;
    if (!p || p >= myargc-1)
	return;
    strncpy (given, myargv[p+1], sizeof given - 1);
    given[sizeof given - 1] = 0;
    part = given;
    for (f = 0; f < LIBRARY_FACTS; f++)
    {
	answers[f] = part;
	part = strchr (part, '/');
	if (!part && f < LIBRARY_FACTS-1)
	    return;
	if (part)
	    *part++ = 0;
    }
    P_LibrarySetReader (answers);
}


int L_Whose (const uint8_t* digits)
{
    const library_format*	format = L_Format ();
    library_number		v;

    if (!known)
	return BOOK_NOBODYS;
    if (!text)
	text = malloc (format->length + 1);
    if (!text)
	return BOOK_NOBODYS;
    Library_Text (format, digits, text);
    if (!Library_ReadBiography (&reader, text, format->length, LIBRARY_LINE, LIBRARY_PAGE_LINES, &v))
	return BOOK_NOBODYS;
    if (!Library_IsTheirs (&modulus, &v, &target))
	return BOOK_NEARLY;
    found = true;
    L_Log ("found his book", players[consoleplayer].mo);
    F_StartLibraryEnding ();
    return BOOK_HIS;
}


size_t L_ReaderSaveSize (void)
{
    return sizeof known + sizeof found + sizeof reader;
}


void L_ArchiveReader (void)
{
    L_Put (&known, sizeof known);
    L_Put (&found, sizeof found);
    L_Put (&reader, sizeof reader);
}


// After the floor, whose seed his target needs.
void L_UnArchiveReader (void)
{
    int		f;

    L_Get (&known, sizeof known);
    L_Get (&found, sizeof found);
    L_Get (&reader, sizeof reader);
    for (f = 0; f < LIBRARY_FACTS; f++)
	reader.fact[f][LIBRARY_FACT_SIZE-1] = 0;
    if (known)
	Aim ();
}
