//
// The library's books (scp_7179_b/README.md, The books): every one stands on
// a shelf, six books across and six rows down: seven to each face of a
// stack, four to each stretch of the back wall between two stacks, 72 to a
// gallery (layout.h); its books the spines the bookcase texture shows,
// counted row by row from the top and from the walkway end (the wall's from
// the gallery's west end, its far side's mirrored);
// the spine the view points at is aimed at (the renderer outlines it, its
// own 3x10 texels) and opened with use. Reading, he stands still and turns the
// pages. The books he has opened are remembered, so none is read twice by
// mistake; that says nothing about where any other book is. Each one opened
// is checked: whether it is his (p_library_reader.c). A real word on
// a page, standing alone (LIBWORDS: scp_7179_b/words), is marked for the
// renderer to show in red; the book is the same either way. (See
// p_library.h, p_library_local.h.)
//

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "i_system.h"
#include "w_wad.h"
#include "z_zone.h"
#include "p_library_local.h"

#define REACH		96		// how far use reaches a book, in map units
#define AISLE		(LIBRARY_GALLERY/LIBRARY_GALLERY_STACKS)	// from one stack's middle to the next

static const int	rowtops[LIBRARY_BOOK_ROWS] = LIBRARY_BOOK_ROW_TOPS;

// A book aimed at: its place on the floor (gallery round the ring, shelf,
// book), and its outline: flat in x on a stack's face (x0 = x1, nudged
// toward the eye), flat in y on the back wall (y0 = y1).
typedef struct
{
    int		gallery, shelf, book;
    float	x0, x1, y0, y1, z0, z1;
} aim_t;

#define HALF_AISLE	((AISLE - LIBRARY_STACK)/2)	// the wall shown either side of an aisle's middle

static boolean		reading;
static uint8_t*		text;		// the open book, as digits; then room for its address
static int		page, pages;
static int		held;		// tics a page key has been held
static boolean		usedown;	// use held since it last opened or closed a book
static int		named = -1;	// the book last named in a message
static char		message[64];
// The real words, as hashes of their digits, open addressed (0: empty).
#define WORDSLOTS	(1 << 17)
#define LONGEST		32		// letters to the longest word looked up
static uint64_t*	words;
// The books he has opened, by floor and place (Key).
static uint64_t*	checked;
static int		numchecked, maxchecked;


static uint64_t Key (const aim_t* aim)
{
    return (uint64_t)(-L_Climbed ()) << 24 | (uint64_t)aim->gallery << 16
	| (uint64_t)aim->shelf << 8 | (uint64_t)aim->book;
}


// The row of books at height z, its spines' bottom and top; -1 on a board.
static int Row (float z, aim_t* aim)
{
    int		v = LIBRARY_STACK_HEIGHT - 1 - (int)floorf (z);	// the texture's row
    int		r;

    for (r = 0; r < LIBRARY_BOOK_ROWS; r++)
	if (v >= rowtops[r] && v < rowtops[r] + LIBRARY_BOOK_HEIGHT)
	{
	    aim->z1 = LIBRARY_STACK_HEIGHT - rowtops[r];
	    aim->z0 = aim->z1 - LIBRARY_BOOK_HEIGHT;
	    return r;
	}
    return -1;
}


// A spine in row r and column col (from the first of a face or a stretch of
// wall) as its shelf, from first, and its book there.
static void Shelve (aim_t* aim, int first, int r, int col)
{
    aim->shelf = first + col/LIBRARY_SHELF_ACROSS;
    aim->book = r*LIBRARY_SHELF_ACROSS + col%LIBRARY_SHELF_ACROSS;
}


// The spine at y and height z on stack face (0-7 in its gallery) that runs
// from y0 to y1, its walkway end at the top (near side) or the bottom (far
// side): its shelf and book, and where it stands; false on the frame or a
// board.
static boolean Spine (float y, float z, float y0, float y1, boolean far, int face, aim_t* aim)
{
    int		base = (int)floorf (y/64)*64, m = (int)floorf (y) - base;	// the texture's column
    int		r, c, tile;

    if (m < LIBRARY_BOOKS_FROM || m >= LIBRARY_BOOKS_FROM + LIBRARY_CASE_BOOKS*LIBRARY_BOOK_WIDTH
	|| (r = Row (z, aim)) < 0)
	return false;
    c = (m - LIBRARY_BOOKS_FROM)/LIBRARY_BOOK_WIDTH;
    aim->y0 = base + LIBRARY_BOOKS_FROM + c*LIBRARY_BOOK_WIDTH;
    aim->y1 = aim->y0 + LIBRARY_BOOK_WIDTH;
    tile = far ? (int)floorf ((y - y0)/64) : (int)floorf ((y1 - 1 - y)/64);
    if (!far)
	c = LIBRARY_CASE_BOOKS-1 - c;
    if (tile < 0 || tile >= LIBRARY_ROW_BOOKS/LIBRARY_CASE_BOOKS)
	return false;
    Shelve (aim, face*LIBRARY_FACE_SHELVES, r, tile*LIBRARY_CASE_BOOKS + c);
    return true;
}


// The spine at x and height z on the back wall: the stretch between two
// stacks it is in (each 16 units into a case: the end of one bookcase, then
// most of the next), its shelf and book, and where it stands; false between
// spines, on a board, or where no gallery's wall is.
static boolean WallSpine (float x, float z, boolean far, aim_t* aim)
{
    int		n = (int)floorf ((x - LIBRARY_LANDING + HALF_AISLE)/AISLE);	// round the ring from the west end
    float	start = LIBRARY_LANDING + n*AISLE - HALF_AISLE;
    int		m = (int)floorf (x - start), u, col, r, g, k;

    if (n < 0 || n >= LIBRARY_SIDE_GALLERIES*LIBRARY_GALLERY_STACKS || (r = Row (z, aim)) < 0)
	return false;
    if (m < HALF_AISLE)
    {
	u = 64 - HALF_AISLE + m;		// the first case, from its texel 16
	if (u < LIBRARY_WALL_FROM || u >= LIBRARY_WALL_FROM + LIBRARY_WALL_BOOKS/2*LIBRARY_BOOK_WIDTH)
	    return false;
	col = (u - LIBRARY_WALL_FROM)/LIBRARY_BOOK_WIDTH;
	aim->x0 = start + (LIBRARY_WALL_FROM - (64 - HALF_AISLE)) + col*LIBRARY_BOOK_WIDTH;
    }
    else
    {
	u = m - HALF_AISLE;			// the next, from its first texel
	if (u < LIBRARY_BOOKS_FROM || u >= LIBRARY_BOOKS_FROM + LIBRARY_WALL_BOOKS/2*LIBRARY_BOOK_WIDTH)
	    return false;
	col = LIBRARY_WALL_BOOKS/2 + (u - LIBRARY_BOOKS_FROM)/LIBRARY_BOOK_WIDTH;
	aim->x0 = start + HALF_AISLE + LIBRARY_BOOKS_FROM + (col - LIBRARY_WALL_BOOKS/2)*LIBRARY_BOOK_WIDTH;
    }
    aim->x1 = aim->x0 + LIBRARY_BOOK_WIDTH;
    g = n/LIBRARY_GALLERY_STACKS;
    k = n%LIBRARY_GALLERY_STACKS;
    if (far)
    {
	g = 2*LIBRARY_SIDE_GALLERIES-1 - g;
	k = LIBRARY_GALLERY_STACKS-1 - k;
	col = LIBRARY_WALL_BOOKS-1 - col;
    }
    aim->gallery = g;
    Shelve (aim, 2*LIBRARY_GALLERY_STACKS*LIBRARY_FACE_SHELVES + k*LIBRARY_WALL_SHELVES, r, col);
    return true;
}


// Whether the line from the eye to (x, y) runs through a stack, whose
// stacks stand from y0 to y1.
static boolean ThroughStack (float ex, float ey, float x, float y, int g0, float y0, float y1)
{
    int		g, k, a;

    for (g = g0-1; g <= g0+1; g++)
	for (k = 0; k < LIBRARY_GALLERY_STACKS; k++)
	{
	    float	mid = LIBRARY_LANDING + g*LIBRARY_GALLERY + AISLE/2 + k*AISLE;
	    float	lo[2], hi[2], from[2], d[2], t0 = 0, t1 = 1;

	    lo[0] = mid - LIBRARY_STACK/2;
	    hi[0] = mid + LIBRARY_STACK/2;
	    lo[1] = y0;
	    hi[1] = y1;
	    from[0] = ex;
	    from[1] = ey;
	    d[0] = x - ex;
	    d[1] = y - ey;
	    for (a = 0; a < 2; a++)
	    {
		float	p, q;

		if (fabsf (d[a]) < 1e-6f)
		{
		    if (from[a] < lo[a] || from[a] > hi[a])
			t0 = 2;
		    continue;
		}
		p = (lo[a] - from[a])/d[a];
		q = (hi[a] - from[a])/d[a];
		if (p > q)
		{
		    float	swap = p;
		    p = q;
		    q = swap;
		}
		t0 = p > t0 ? p : t0;
		t1 = q < t1 ? q : t1;
	    }
	    if (t0 < t1 - 1e-4f)
		return true;
	}
    return false;
}


static boolean Checked (uint64_t key)
{
    int		i;

    for (i = 0; i < numchecked; i++)
	if (checked[i] == key)
	    return true;
    return false;
}


static void Remember (uint64_t key)
{
    if (Checked (key))
	return;
    if (numchecked == maxchecked)
    {
	uint64_t*	more = realloc (checked, (maxchecked ? 2*maxchecked : 256)*sizeof *checked);
	if (!more)
	    return;
	checked = more;
	maxchecked = maxchecked ? 2*maxchecked : 256;
    }
    checked[numchecked++] = key;
}


static uint64_t Hash (const uint8_t* digits, int n)
{
    uint64_t	h = 1469598103934665603ull;
    int		i;

    for (i = 0; i < n; i++)
    {
	h ^= digits[i] + 1u;
	h *= 1099511628211ull;
    }
    return h | 1;
}


static boolean IsWord (const uint8_t* digits, int n)
{
    uint64_t	h = Hash (digits, n);
    unsigned	slot = (unsigned)(h >> 40) & (WORDSLOTS-1);

    if (!words)
	return false;
    while (words[slot])
    {
	if (words[slot] == h)
	    return true;
	slot = (slot + 1) & (WORDSLOTS-1);
    }
    return false;
}


// The word list, once: each line a word in the alphabet's letters.
static void ReadWords (void)
{
    const library_format*	format = L_Format ();
    int				lump = W_CheckNumForName ("LIBWORDS");
    const char*			at;
    const char*			end;
    uint8_t			digits[LONGEST];
    int				n;

    if (words || lump < 0)
	return;
    words = calloc (WORDSLOTS, sizeof *words);
    if (!words)
	return;
    at = W_CacheLumpNum (lump, PU_STATIC);
    end = at + W_LumpLength (lump);
    while (at < end)
    {
	n = 0;
	for (; at < end && *at != '\n' && *at != '\r'; at++)
	{
	    const char*	c = strchr (format->alphabet, *at);
	    if (n < LONGEST && c && *at)
		digits[n] = (uint8_t)(c - format->alphabet);
	    n++;
	}
	for (; at < end && (*at == '\n' || *at == '\r'); at++)
	    ;
	if (n > 0 && n <= LONGEST)
	{
	    uint64_t	h = Hash (digits, n);
	    unsigned	slot = (unsigned)(h >> 40) & (WORDSLOTS-1);
	    while (words[slot] && words[slot] != h)
		slot = (slot + 1) & (WORDSLOTS-1);
	    words[slot] = h;
	}
    }
}


static boolean Letter (uint8_t digit)
{
    char	c = L_Format ()->alphabet[digit];
    return c >= 'a' && c <= 'z';
}


// The book the view points at within reach, if any: the nearest stack face
// the eye's ray meets from outside, in the galleries round him, or the back
// wall behind them where no stack stands between.
static boolean Aim (player_t* player, aim_t* aim)
{
    mobj_t*	mo = player->mo;
    float	ex = mo->x/(float)FRACUNIT, ey = mo->y/(float)FRACUNIT, ez = player->viewz/(float)FRACUNIT;
    float	an = mo->angle*(6.283185307179586f/4294967296.0f);
    float	dx = cosf (an)*cosf (tourpitch), dy = sinf (an)*cosf (tourpitch), dz = sinf (tourpitch);
    boolean	far = ey > LIBRARY_NEAR_RAIL + LIBRARY_CHASM/2;
    float	y0 = far ? LIBRARY_FAR_WALL - LIBRARY_STACKS : LIBRARY_CORNICE;
    float	y1 = far ? LIBRARY_FAR_WALL - LIBRARY_CORNICE : LIBRARY_STACKS;
    float	best = REACH;
    int		g0 = (int)floorf ((ex - LIBRARY_LANDING)/LIBRARY_GALLERY);
    int		g, k, side;
    boolean	found = false;
    aim_t	at;
    float	wall = far ? LIBRARY_FAR_WALL - LIBRARY_CORNICE : LIBRARY_CORNICE;

    for (g = g0-1; g <= g0+1; g++)
    {
	if (g < 0 || g >= LIBRARY_SIDE_GALLERIES)
	    continue;
	for (k = 0; k < LIBRARY_GALLERY_STACKS; k++)
	    for (side = 0; side < 2; side++)
	    {
		float	fx = LIBRARY_LANDING + g*LIBRARY_GALLERY + AISLE/2 + k*AISLE
			     + (side ? LIBRARY_STACK/2 : -LIBRARY_STACK/2);
		float	t, y, z;

		// Only a face turned toward him: the west face from the west.
		if (side ? dx >= 0 : dx <= 0)
		    continue;
		t = (fx - ex)/dx;
		y = ey + t*dy;
		z = ez + t*dz;
		if (t <= 0 || t >= best || y < y0 || y >= y1 || z < 0 || z >= LIBRARY_STACK_HEIGHT)
		    continue;
		// The nearest face hit decides, a spine on it or not.
		best = t;
		found = Spine (y, z, y0, y1, far, 2*k + side, &at);
		if (!found)
		    continue;
		at.gallery = far ? 2*LIBRARY_SIDE_GALLERIES-1 - g : g;
		at.x0 = at.x1 = fx + (side ? 0.5f : -0.5f);
		*aim = at;
	    }
    }
    // The back wall, toward it from an aisle.
    if (far ? dy > 0 : dy < 0)
    {
	float	t = (wall - ey)/dy, x = ex + t*dx, z = ez + t*dz;

	if (t > 0 && t < best && z >= 0 && z < LIBRARY_STACK_HEIGHT
	    && !ThroughStack (ex, ey, x, wall, g0, y0, y1))
	{
	    found = WallSpine (x, z, far, &at);
	    if (found)
	    {
		at.y0 = at.y1 = wall + (far ? -0.5f : 0.5f);
		*aim = at;
	    }
	}
    }
    return found;
}


static void Open (player_t* player, const aim_t* aim)
{
    const library_format*	format = L_Format ();
    library_place		place;
    int				whose;

    if (!text)
	text = malloc (2*format->length);
    place.gallery = aim->gallery;
    place.shelf = aim->shelf;
    place.book = aim->book;
    if (!text || !Library_Join (format, L_Floor (), &place, text + format->length))
	return;
    Library_BookAt (format, L_Seed (), text + format->length, text);
    reading = true;
    page = 0;
    pages = (int)(format->length / (LIBRARY_PAGE_LINES*LIBRARY_LINE));
    held = 0;
    Remember (Key (aim));
    whose = L_Whose (text);
    snprintf (message, sizeof message, "%sPAGE 1 OF %d", whose == BOOK_HIS ? "THIS IS YOUR BOOK. "
	      : whose == BOOK_NEARLY ? "THIS IS NEARLY YOUR LIFE. " : "", pages);
    player->message = message;
    L_Log ("opened a book", player->mo);
}


// Turning the pages: forward and back one, sideways ten; held, they go on.
static void Turn (player_t* player, ticcmd_t* cmd)
{
    int		by = cmd->forwardmove > 0 ? 1 : cmd->forwardmove < 0 ? -1
		   : cmd->sidemove > 0 ? 10 : cmd->sidemove < 0 ? -10 : 0;

    if (!by)
    {
	held = 0;
	return;
    }
    if (held == 0 || (held >= 10 && held % 3 == 0))
    {
	page += by;
	page = page < 0 ? 0 : page >= pages ? pages-1 : page;
	snprintf (message, sizeof message, "PAGE %d OF %d", page+1, pages);
	player->message = message;
    }
    held++;
}


boolean P_LibraryBooks (player_t* player)
{
    ticcmd_t*	cmd = &player->cmd;
    boolean	use;
    aim_t	aim;
    int		key;

    if (!P_Library () || !L_Floor () || player != &players[consoleplayer]
	|| player->playerstate != PST_LIVE || (cmd->buttons & BT_SPECIAL))
    {
	if (player == &players[consoleplayer])
	    reading = false;
	return false;
    }
    use = (cmd->buttons & BT_USE) != 0;
    if (!use)
	usedown = false;

    if (reading)
    {
	if (use && !usedown)
	{
	    usedown = true;
	    reading = false;
	}
	else
	    Turn (player, cmd);
	// He stands still over the book: nothing else of the tic is his.
	cmd->forwardmove = cmd->sidemove = 0;
	cmd->angleturn = 0;
	cmd->buttons = 0;
	return true;
    }

    if (usedown)
	cmd->buttons &= ~BT_USE;	// still held from closing a book
    if (!Aim (player, &aim))
    {
	named = -1;
	return false;
    }
    key = (aim.gallery << 16) | (aim.shelf << 8) | aim.book;
    if (key != named)
    {
	named = key;
	snprintf (message, sizeof message, "SHELF %d, BOOK %d%s", aim.shelf+1, aim.book+1,
		  Checked (Key (&aim)) ? " - READ" : "");
	player->message = message;
    }
    if (use && !usedown)
    {
	usedown = true;
	cmd->buttons &= ~BT_USE;
	Open (player, &aim);
    }
    return false;
}


boolean P_LibraryAim (float corners[4][3], boolean* read)
{
    player_t*	player = &players[consoleplayer];
    aim_t	aim;

    if (!P_Library () || reading || !player->mo || player->playerstate != PST_LIVE || !Aim (player, &aim))
	return false;
    // Clockwise from the top left: across the face, then down.
    corners[0][0] = corners[3][0] = aim.x0;
    corners[1][0] = corners[2][0] = aim.x1;
    corners[0][1] = corners[3][1] = aim.y0;
    corners[1][1] = corners[2][1] = aim.y1;
    corners[0][2] = corners[1][2] = aim.z1;
    corners[2][2] = corners[3][2] = aim.z0;
    *read = Checked (Key (&aim));
    return true;
}


int P_LibraryPage (char* lines, char* marks)
{
    const library_format*	format = L_Format ();
    size_t			from = (size_t)page*LIBRARY_PAGE_LINES*LIBRARY_LINE;
    size_t			to = from + LIBRARY_PAGE_LINES*LIBRARY_LINE, a, b, i;
    int				l;

    if (!P_Library () || !reading || !text)
	return 0;
    for (l = 0; l < LIBRARY_PAGE_LINES; l++)
    {
	for (i = 0; i < LIBRARY_LINE; i++)
	{
	    lines[l*(LIBRARY_LINE+1) + i] = format->alphabet[text[from + l*LIBRARY_LINE + i]];
	    marks[l*(LIBRARY_LINE+1) + i] = 0;
	}
	lines[l*(LIBRARY_LINE+1) + LIBRARY_LINE] = 0;
    }
    // Each run of letters touching the page, whole as the book has it
    // (across the page's edges too): marked where it is a real word.
    for (i = from; i < to; i = b)
    {
	if (!Letter (text[i]))
	{
	    b = i + 1;
	    continue;
	}
	for (a = i; a > 0 && Letter (text[a-1]); a--)
	    ;
	for (b = i + 1; b < format->length && Letter (text[b]); b++)
	    ;
	if (b - a <= LONGEST && IsWord (text + a, (int)(b - a)))
	    for (a = a < from ? from : a; a < b && a < to; a++)
		marks[(a - from)/LIBRARY_LINE*(LIBRARY_LINE+1) + (a - from)%LIBRARY_LINE] = 1;
    }
    return page + 1;
}


// A new level: no book open (the books he opened stay remembered only
// through a savegame).
void L_BooksLevel (void)
{
    ReadWords ();
    reading = false;
    numchecked = 0;
    named = -1;
    usedown = false;
}


size_t L_BooksSaveSize (void)
{
    return sizeof numchecked + (size_t)numchecked*sizeof *checked;
}


void L_ArchiveBooks (void)
{
    L_Put (&numchecked, sizeof numchecked);
    L_Put (checked, (size_t)numchecked*sizeof *checked);
}


void L_UnArchiveBooks (void)
{
    int		n, i;
    uint64_t	key;

    numchecked = 0;
    reading = false;
    L_Get (&n, sizeof n);
    if (n < 0)
	I_Error ("Bad savegame: the library's books");
    for (i = 0; i < n; i++)
    {
	L_Get (&key, sizeof key);
	Remember (key);
    }
}
