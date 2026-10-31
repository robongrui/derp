//
// The end of the library (f_library.c): a finale of its own once he has
// found his book. F_Ticker and F_Drawer hand over to it while it runs; any
// other finale, or a new game, stops it.
//

#ifndef __F_LIBRARY__
#define __F_LIBRARY__

#include "doomtype.h"

void F_StartLibraryEnding (void);
void F_StopLibraryEnding (void);
boolean F_LibraryTicker (void);
boolean F_LibraryDrawer (void);

#endif
