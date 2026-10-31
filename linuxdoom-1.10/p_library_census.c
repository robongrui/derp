//
// The library's census (scp_7179_b/README.md, People): how crowded a floor
// is, from the number of people who have ever died, as the Population
// Reference Bureau counts the births (Table 1, "How Many People Have Ever
// Lived on Earth?", November 2022: 117 billion), and how far down they
// could have searched by now.
//
// Everyone arrives at the top a lifetime (LIFETIME years) after being born,
// and searches downward, giving each book a glance (GLANCE seconds) and
// never sleeping: a floor of 259,200 books takes six days, about 61 floors
// a year. Someone here T years is anywhere on the floors he could have
// reached, 1 to 61 T, as likely one as another. So a floor holds the share
// of everyone who arrived long enough ago to reach it, each counted thinly
// by how far he could have gone; the top is the most crowded, most people
// having died lately. Relative to it: floor 10 two thirds as crowded,
// floor 100 under half, floor 1,000 a quarter, floor 10,000 one in fifteen,
// floor 1,000,000 almost no one, and below floor 11,713,586 (where the
// first of us would be by now) no one at all. Those who jumped are a
// different matter (p_library_fallers.c): falling limp the first of them
// would be some 44 trillion floors down.
//

#include <math.h>

#include "doomdef.h"
#include "p_library_local.h"

#define NOW		2026	// the year it is (well, roughly)
#define LIFETIME	30	// years from birth to here, on the whole
#define GLANCE		2	// seconds a book takes to rule out
#define YEAR		(365.25*86400)

// PRB's benchmarks: the year (B.C.E. negative) and those born since the one before.
static const struct { double year, born; } prb[] =
{
    {-190000, 0}, {-50000, 7856100000.0}, {-8000, 1137789769.0}, {1, 46025332354.0},
    {1200, 26591343000.0}, {1650, 12782002453.0}, {1750, 3171931513.0}, {1850, 4046240009.0},
    {1900, 2900237856.0}, {1950, 3390198215.0}, {2000, 6064994884.0}, {2010, 1364003405.0},
    {2022, 1690275115.0},
};


// Floors a searcher goes down in a year.
static double Pace (void)
{
    double	books = 2.0*LIBRARY_SIDE_GALLERIES*LIBRARY_GALLERY_SHELVES*LIBRARY_SHELF_BOOKS;

    return YEAR/(books*GLANCE);
}


// How many people are on floor n (1 the top), in people: for each stretch
// of PRB's table, arriving at a steady rate, the integral of rate/(pace T)
// over those here long enough (T >= n/pace); those not yet past floor 1
// all on it.
static double Souls (double n)
{
    double	pace = Pace (), d = 0;
    int		i;

    for (i = 1; i < (int)(sizeof prb/sizeof prb[0]); i++)
    {
	double	from = prb[i-1].year + LIFETIME, to = prb[i].year + LIFETIME;
	double	rate = prb[i].born/(prb[i].year - prb[i-1].year);
	double	t0, t1, lo;

	if (to > NOW)
	    to = NOW;
	if (from >= to)
	    continue;
	t0 = NOW - to;		// years here, the latest of them
	t1 = NOW - from;	// and the earliest
	lo = n/pace > t0 ? n/pace : t0;
	if (lo < 1e-9)
	    lo = 1e-9;
	if (lo < t1)
	    d += rate/pace*log (t1/lo);
	if (n <= 1 && t0 < 1/pace)
	    d += rate*((t1 < 1/pace ? t1 : 1/pace) - t0);
    }
    return d;
}


float L_Crowd (long long climbed)
{
    static long long	known = 1;
    static float	crowd = 1;
    static double	top;
    double		n = 1.0 - (double)climbed;	// climbed 0 is floor 1, the top

    if (L_Deep ())
	return 0;		// past the count's 64 bits: deeper than anyone got
    if (climbed == known)
	return crowd;
    if (!top)
	top = Souls (1);
    known = climbed;
    crowd = (float)(Souls (n < 1 ? 1 : n)/top);
    return crowd;
}
