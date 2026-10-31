//
// The kiosk's menu (scp_7179_b/README.md): use facing the kiosk opens it, and
// the game waits while he chooses. Everything is free: each weapon the game
// has, each ammunition, a backpack, endless ammunition, health, armour, or
// all of it; what he takes goes to the game as a tic command (sendkiosk,
// p_library_kiosk.c). And where he is, to tell others if he likes. Shown
// as a page, the way an open book is (Game_LibraryPage: the renderer's
// panel), the chosen row in red.
//

#include <stdio.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "i_system.h"
#include "s_sound.h"
#include "sounds.h"
#include "p_library.h"

extern boolean	menuactive;
extern int	sendkiosk;

#define WIDTH		LIBRARY_LINE
#define HEIGHT		LIBRARY_PAGE_LINES
#define ROW_FROM	4			// the first row's line

enum { NOTHING = 100 };				// a row that isn't an order

typedef struct
{
    char	text[WIDTH+1];
    int		order;				// KIOSK_*, or NOTHING (a heading)
} row_t;

static boolean		open;
static boolean		seenany;		// it has opened...
static int		seen;			// ...for this use (P_LibraryKioskVisited)
static int		chosen;			// a row
static row_t		rows[HEIGHT];
static int		numrows;

static const struct { int weapon; const char* name; } weapons[] =
{
    {wp_fist, "FIST"}, {wp_chainsaw, "CHAINSAW"}, {wp_pistol, "PISTOL"},
    {wp_shotgun, "SHOTGUN"}, {wp_supershotgun, "SUPER SHOTGUN"},
    {wp_chaingun, "CHAINGUN"}, {wp_missile, "ROCKET LAUNCHER"},
    {wp_plasma, "PLASMA RIFLE"}, {wp_bfg, "BFG 9000"},
};
static const char* const ammo[NUMAMMO] = {"BULLETS", "SHELLS", "CELLS", "ROCKETS"};


// A row: what it is on the left, how it stands on the right.
static void Row (int order, const char* left, const char* right)
{
    row_t*	r;

    if (numrows == HEIGHT)
	return;
    r = &rows[numrows++];
    r->order = order;
    snprintf (r->text, sizeof r->text, "%-44s%s", left, right);
}


// The rows as they stand now: what he has, what is on.
static void Build (void)
{
    player_t*	p = &players[consoleplayer];
    char	left[WIDTH+1];
    int		i;

    numrows = 0;
    Row (NOTHING, "WEAPONS", "");
    for (i = 0; i < (int)(sizeof weapons/sizeof weapons[0]); i++)
	if (P_LibraryHasWeapon (weapons[i].weapon))
	{
	    snprintf (left, sizeof left, "    %s", weapons[i].name);
	    Row (KIOSK_WEAPON + weapons[i].weapon, left, p->weaponowned[weapons[i].weapon] ? "YOURS" : "FREE");
	}
    Row (NOTHING, "AMMUNITION", "");
    for (i = 0; i < NUMAMMO; i++)
    {
	snprintf (left, sizeof left, "    %-24s%4d / %d", ammo[i], p->ammo[i], p->maxammo[i]);
	Row (KIOSK_AMMO + i, left, "FREE");
    }
    Row (KIOSK_ALL_AMMO, "    ALL OF IT", "FREE");
    Row (KIOSK_BACKPACK, "    A BACKPACK, TO CARRY TWICE AS MUCH", p->backpack ? "YOURS" : "FREE");
    Row (KIOSK_ENDLESS, "    AMMUNITION THAT NEVER RUNS OUT", P_LibraryEndless () ? "ON" : "OFF");
    snprintf (left, sizeof left, "HEALTH%26d%%", p->health);
    Row (KIOSK_HEALTH, left, "FREE");
    snprintf (left, sizeof left, "ARMOUR%26d%%", p->armorpoints);
    Row (KIOSK_ARMOR, left, "FREE");
    Row (KIOSK_EVERYTHING, "EVERYTHING", "FREE");
    Row (NOTHING, "", "");
    Row (NOTHING, "WHERE YOU ARE", "");
    snprintf (left, sizeof left, "    %s", P_LibraryNote ());
    Row (NOTHING, left, "");
}


static void Move (int by)
{
    int		i, at = chosen;

    for (i = 0; i < numrows; i++)
    {
	at = (at + by + numrows) % numrows;
	if (rows[at].order != NOTHING)
	{
	    chosen = at;
	    return;
	}
    }
}


void M_KioskTicker (void)
{
    int		visited;
    boolean	used = P_LibraryKioskVisited (&visited);

    if (open && (gamestate != GS_LEVEL || !P_Library ()))
	open = menuactive = false;
    if (open || !used || (seenany && visited == seen) || L_SINCE (visited) > 1
	|| demoplayback || gamestate != GS_LEVEL || menuactive)
	return;
    seen = visited, seenany = true;
    open = menuactive = true;
    Build ();
    chosen = 0;
    Move (1);
}


static void Take (void)
{
    int		order = rows[chosen].order;

    if (order != NOTHING)
	sendkiosk = order;
    S_StartSound (NULL, sfx_pistol);
}


boolean M_KioskResponder (int ch)
{
    if (!open)
	return false;
    if (ch == KEY_UPARROW)
	Move (-1);
    else if (ch == KEY_DOWNARROW)
	Move (1);
    else if (ch == KEY_ENTER)
	Take ();
    else if (ch == KEY_ESCAPE || ch == KEY_BACKSPACE)
    {
	open = menuactive = false;
	S_StartSound (NULL, sfx_swtchx);
    }
    return true;
}


// While it is open the menus draw nothing: the page is the renderer's.
boolean M_KioskDrawer (void)
{
    return open;
}


// The page: lines of WIDTH characters and a 0, the chosen row marked.
int M_KioskPage (char* lines, char* marks)
{
    static const char* const help = "UP, DOWN: CHOOSE      ENTER: TAKE      ESCAPE: LEAVE";
    int		l, i, at;

    if (!open)
	return 0;
    Build ();
    memset (marks, 0, HEIGHT*(WIDTH+1));
    for (l = 0; l < HEIGHT; l++)
    {
	char*	line = lines + l*(WIDTH+1);

	memset (line, ' ', WIDTH);
	line[WIDTH] = 0;
    }
    memcpy (lines + 1*(WIDTH+1) + (WIDTH - 9)/2, "THE KIOSK", 9);
    memcpy (lines + 2*(WIDTH+1) + (WIDTH - 23)/2, "EVERYTHING HERE IS FREE", 23);
    for (i = 0; i < numrows && ROW_FROM + i < HEIGHT - 2; i++)
    {
	at = (ROW_FROM + i)*(WIDTH+1) + 4;
	memcpy (lines + at, rows[i].text, strlen (rows[i].text) < WIDTH - 8 ? strlen (rows[i].text) : WIDTH - 8);
	if (i == chosen)
	    memset (marks + at, 1, WIDTH - 8);
    }
    memcpy (lines + (HEIGHT-1)*(WIDTH+1) + (WIDTH - (int)strlen (help))/2, help, strlen (help));
    return 1;
}
