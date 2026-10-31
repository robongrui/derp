//
// The library's status bar face (scp_7179_b/README.md, The player): he is a
// zombieman, the same body as everyone there, so the face is a zombieman's.
// No new art: his head and shoulders cut from the zombieman's front frame
// (POSSA1), doubled, its bottom corners rounded off, the colours drained
// toward grey and dimmed, in the middle of the face box. It doesn't
// glance, grin or wince: it only looks.
//

#include <string.h>

#include "doomdef.h"
#include "m_swap.h"
#include "r_defs.h"
#include "w_wad.h"
#include "z_zone.h"
#include "st_library.h"

#define FROM_X		9		// the bust in POSSA1: its columns from here,
#define FROM_Y		0		// its rows from the top,
#define BUST		13		// that many each way (not his gun or arms)
#define SCALE		2
#define SIZE		(BUST*SCALE)
#define ROUND		4		// the bottom corners' radius, in his own pixels
#define BOX_X0		141		// the face box in the status bar, inside its frame
#define BOX_X1		177
#define BOX_Y0		169		// its rows on the screen
#define BOX_Y1		199
#define FACE_X		143		// where the bar draws a face (ST_FACESX, ST_FACESY)
#define FACE_Y		168
#define DULL		0.45f		// share of grey
#define DIM		0.85f

static patch_t*		face;


// Each colour toward grey and darker, as its nearest in the palette.
static void Dulled (const byte* pal, byte* table)
{
    int		i, j, best, d, nearest;

    for (i = 0; i < 256; i++)
    {
	float	grey = pal[3*i]*0.30f + pal[3*i+1]*0.59f + pal[3*i+2]*0.11f;
	float	want[3];

	for (j = 0; j < 3; j++)
	    want[j] = (pal[3*i+j]*(1 - DULL) + grey*DULL)*DIM;
	for (best = 0, nearest = 1 << 30, j = 0; j < 256; j++)
	{
	    float	dr = pal[3*j] - want[0], dg = pal[3*j+1] - want[1], db = pal[3*j+2] - want[2];

	    d = (int)(dr*dr + dg*dg + db*db);
	    if (d < nearest)
		nearest = d, best = j;
	}
	table[i] = (byte)best;
    }
}


patch_t* ST_LibraryFace (void)
{
    short	pixels[BUST][BUST];		// palette index, or -1 where he isn't
    byte	table[256];
    patch_t*	head;
    int		lump, x, y, size, at;
    byte*	out;

    if (face)
	return face;
    lump = W_CheckNumForName ("POSSA1");
    if (lump < 0)
	return NULL;
    head = W_CacheLumpNum (lump, PU_CACHE);
    memset (pixels, 0xff, sizeof pixels);
    for (x = 0; x < BUST && FROM_X + x < SHORT (head->width); x++)
    {
	column_t*	column = (column_t*)((byte*)head + LONG (head->columnofs[FROM_X + x]));

	for (; column->topdelta != 0xff; column = (column_t*)((byte*)column + column->length + 4))
	    for (y = 0; y < column->length; y++)
		if (column->topdelta + y - FROM_Y >= 0 && column->topdelta + y - FROM_Y < BUST)
		    pixels[column->topdelta + y - FROM_Y][x] = ((byte*)column)[3 + y];
    }
    // The shoulders' cut corners rounded: what lies past a quarter circle
    // in each.
    for (y = BUST - ROUND; y < BUST; y++)
	for (x = 0; x < ROUND; x++)
	{
	    float	dx = ROUND - x - 0.5f, dy = y - (BUST - ROUND) + 0.5f;

	    if (dx*dx + dy*dy > ROUND*ROUND)
		pixels[y][x] = pixels[y][BUST-1 - x] = -1;
	}
    Dulled (W_CacheLumpName ("PLAYPAL", PU_CACHE), table);

    // A patch of SIZE columns, each its runs of him (at most SIZE/2 runs of
    // at least one pixel, each 4 bytes over its pixels, and the end).
    size = 8 + 4*SIZE + SIZE*(SIZE + 4*SIZE/2 + 1);
    face = Z_Malloc (size, PU_STATIC, NULL);
    face->width = SHORT (SIZE);
    face->height = SHORT (SIZE);
    // Drawn at the bar's face place: in the middle of the box.
    face->leftoffset = SHORT (FACE_X - (BOX_X0 + (BOX_X1 - BOX_X0 + 1 - SIZE)/2));
    face->topoffset = SHORT (FACE_Y - (BOX_Y0 + (BOX_Y1 - BOX_Y0 + 1 - SIZE)/2));
    out = (byte*)face;
    at = 8 + 4*SIZE;
    for (x = 0; x < SIZE; x++)
    {
	face->columnofs[x] = LONG (at);
	for (y = 0; y < SIZE; )
	{
	    int		start;

	    if (pixels[y/SCALE][x/SCALE] < 0)
	    {
		y++;
		continue;
	    }
	    for (start = y; y < SIZE && pixels[y/SCALE][x/SCALE] >= 0; y++)
		;
	    out[at++] = start;
	    out[at++] = y - start;
	    out[at++] = 0;
	    for (; start < y; start++)
		out[at++] = table[pixels[start/SCALE][x/SCALE]];
	    out[at++] = 0;
	}
	out[at++] = 0xff;
    }
    return face;
}
