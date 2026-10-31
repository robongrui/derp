//
// The questions in the library (scp_7179_b/README.md, Your book): when he
// first stands there, the game waits while he says who he is. A question at
// a time, typed like a savegame's name; Escape goes back one (from the first,
// to the main menu, and they come back when it closes); the last screen shows
// all of it before it counts. Then two pages: where he is and why, his one
// task, and how to get about (not how to get over the railing: that he
// learns from the others). And after his first death, once a game: he
// can not die again. All of it on black. His book is the biography of
// these answers (p_library_reader.c). Not in recorded games:
// -libraryreader answers them.
//

#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "hu_stuff.h"
#include "m_menu.h"
#include "s_sound.h"
#include "sounds.h"
#include "v_video.h"
#include "p_library.h"
#include "st_stuff.h"

#define ANSWER		24		// characters to an answer
#define CONFIRM		LIBRARY_QUESTIONS	// the step that shows them all

void M_WriteText (int x, int y, char* string);
int M_StringWidth (char* string);
extern boolean menuactive;

static const char* const questions[LIBRARY_QUESTIONS] =
{
    "WHAT IS YOUR NAME?",
    "WHAT YEAR WERE YOU BORN?",
    "WHERE WERE YOU BORN?",
    "WHERE DID YOU GROW UP?",
    "WHAT IS YOUR STAR SIGN?",
    "WHAT IS YOUR PET'S NAME?",
    "WHAT IS YOUR FAVOURITE FOOD?",
};

static const char* const labels[LIBRARY_QUESTIONS] =
{
    "NAME", "BORN", "BORN IN", "GREW UP IN", "STAR SIGN", "PET", "FOOD",
};

static boolean		asking;
static int		welcoming;	// the page of the welcome after (1, 2), or 0
static boolean		telling;	// after his first death: he can't die again
static boolean		toldany;	// a death told of...
static int		told;		// ...the one (its leveltime): once
static int		step;
static char		answers[LIBRARY_QUESTIONS][ANSWER+1];
static const char*	trouble;	// why the last answer wasn't taken


static void Ask (void)
{
    asking = true;
    menuactive = true;
    step = 0;
    trouble = NULL;
    memset (answers, 0, sizeof answers);
}


void M_LibraryTicker (void)
{
    int		when, deaths = P_Library () ? P_LibraryDeaths (&when) : 0;

    // A screen of ours is up only while the menus are: closed some other way,
    // it is gone (and the view must not stay black for it).
    if ((welcoming || telling) && (gamestate != GS_LEVEL || !menuactive))
    {
	welcoming = 0;
	telling = false;
	ST_Refresh ();
    }
    // The first death, as it happens (not in a game loaded after it).
    if (deaths == 1 && L_SINCE (when) <= 1 && !(toldany && when == told) && !telling && !menuactive
	&& !demoplayback && gamestate == GS_LEVEL)
    {
	told = when, toldany = true;
	telling = menuactive = true;
    }
    if (!asking && !menuactive && P_LibraryNeedsReader ())
	Ask ();
    if (asking && !P_LibraryNeedsReader ())
    {
	asking = false;		// the level ended or a game was loaded
	menuactive = false;
    }
}


static void Next (void)
{
    const char*	answer = answers[step];
    const char*	c;

    if (step == CONFIRM)
    {
	const char*	given[LIBRARY_QUESTIONS];
	int		i;

	for (i = 0; i < LIBRARY_QUESTIONS; i++)
	    given[i] = answers[i];
	if (P_LibrarySetReader (given))
	{
	    asking = false;
	    welcoming = 1;
	    S_StartSound (NULL, sfx_swtchx);
	}
	return;
    }
    if (!P_LibraryAnswerFits (step, answer))
    {
	for (c = answer; *c && (*c < '0' || *c > '9'); c++)
	    ;
	trouble = step == 1 ? "THE YEAR, IN DIGITS"
	    : *c ? "NUMBERS IN WORDS, PLEASE"
	    : "SAY SOMETHING";
	S_StartSound (NULL, sfx_oof);
	return;
    }
    trouble = NULL;
    step++;
    S_StartSound (NULL, sfx_pistol);
}


boolean M_LibraryResponder (int ch)
{
    char*	answer;
    size_t	length;

    if (telling)
    {
	if (ch == KEY_ENTER || ch == KEY_ESCAPE)
	{
	    telling = menuactive = false;
	    ST_Refresh ();
	    S_StartSound (NULL, sfx_swtchx);
	}
	return true;
    }
    if (welcoming)
    {
	if (ch == KEY_ENTER && ++welcoming > 2)
	{
	    welcoming = 0, menuactive = false;
	    ST_Refresh ();
	}
	else if (ch == KEY_ESCAPE && welcoming > 1)
	    welcoming--;
	else
	    return true;
	S_StartSound (NULL, sfx_swtchx);
	return true;
    }
    if (!asking)
	return false;
    if (ch == KEY_ENTER)
    {
	Next ();
	return true;
    }
    if (ch == KEY_ESCAPE)
    {
	trouble = NULL;
	if (step > 0)
	    step--;
	else
	{
	    asking = false;
	    menuactive = false;
	    M_StartControlPanel ();
	}
	S_StartSound (NULL, sfx_swtchx);
	return true;
    }
    if (step == CONFIRM)
	return true;
    answer = answers[step];
    length = strlen (answer);
    if (ch == KEY_BACKSPACE)
    {
	if (length)
	    answer[length-1] = 0;
	return true;
    }
    if (ch >= 'a' && ch <= 'z')
	ch -= 'a' - 'A';
    if (ch != ' ' && (ch < HU_FONTSTART || ch > HU_FONTEND))
	return true;
    if (length < ANSWER)
    {
	answer[length] = (char)ch;
	answer[length+1] = 0;
    }
    return true;
}


static void Centered (int y, const char* text)
{
    M_WriteText (160 - M_StringWidth ((char*)text)/2, y, (char*)text);
}


// The welcome: where he is and why, then how to get about. "%N" is his
// name as he gave it.
static const char* const welcome[2][16] =
{
    {
	"WELCOME TO HELL,", "%N.", "",
	"YOU ARE HERE BECAUSE YOU WERE WRONG.", "NOT ABOUT ANYTHING IN PARTICULAR.",
	"EVERYONE WAS. IT WAS AN EASY MISTAKE.", "",
	"THIS IS A LIBRARY. IT HOLDS EVERY", "BOOK THAT CAN BE WRITTEN. ONE OF",
	"THEM IS THE STORY OF YOUR LIFE,", "EVERY WORD OF IT TRUE.", "",
	"YOU HAVE ONE TASK: FIND YOUR BOOK.", "FIND IT, AND YOU CAN LEAVE.", NULL,
    },
    {
	"LOOK AT A BOOK AND USE IT TO READ.", "FORWARD, BACK: A PAGE. STRAFE: TEN.",
	"USE AGAIN TO PUT IT BACK.", "",
	"THE KIOSK HAS ALL YOU NEED.", "WHEN YOU ARE TIRED, FIND A BED.", "",
	"IF YOU FALL:", "STEER WITH YOUR MOVEMENT KEYS.", "LET GO OF THEM TO STOP DRIFTING.",
	"LOOK DOWN TO FALL FASTER,", "AHEAD TO FALL SLOWER.", NULL,
    },
};


// Whether the questions or the welcome are up: the renderer's view goes
// black too (Game_LibraryEyes), beside the menus' layer on a wide screen.
boolean M_LibraryBlack (void)
{
    return (asking || welcoming || telling) && menuactive;
}


// All black, the status bar too: the menus' layer covers the view.
static void Black (void)
{
    memset (screens[0], 0, SCREENWIDTH*SCREENHEIGHT);	// PLAYPAL's first colour: black
    memset (v_overlaymask, 255, sizeof v_overlaymask);
}


boolean M_LibraryDrawer (void)
{
    char	line[ANSWER+2];
    int		i, height = 11;		// the small font and a gap

    if (telling)
    {
	Black ();
	Centered (92, "YOU CAN NOT DIE AGAIN.");
	Centered (186, "ENTER");
	return true;
    }
    if (welcoming)
    {
	const char* const*	page = welcome[welcoming-1];

	Black ();
	for (i = 0; page[i]; i++)
	    Centered (14 + i*height, strcmp (page[i], "%N.") ? page[i] : strcat (strcpy (line, answers[0]), "."));
	Centered (186, welcoming == 1 ? "ENTER" : "ENTER: BEGIN");
	return true;
    }
    if (!asking)
	return false;
    Black ();
    // What he has said so far, and the question.
    for (i = 0; i < step; i++)
    {
	M_WriteText (40, 16 + i*height, (char*)labels[i]);
	M_WriteText (130, 16 + i*height, answers[i][0] ? answers[i] : "-");
    }
    if (step == CONFIRM)
    {
	Centered (120, "IS THIS YOU?");
	Centered (140, "ENTER: YES    ESCAPE: CHANGE IT");
	return true;
    }
    Centered (120, questions[step]);
    strcpy (line, answers[step]);
    strcat (line, "_");
    Centered (136, line);
    if (trouble)
	Centered (156, trouble);
    else if (step == 5)
	Centered (156, "NONE IF YOU HAVE NONE");
    return true;
}
