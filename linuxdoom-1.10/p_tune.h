// A tuning number. In a shipped build TUNE is the plain constant. In a build with
// the private dev tools (they set DOOM_TUNE) the AI graph can change it while the
// game runs; the same tool reads these from the source for their names and words, so
// a #define stays where it was, next to the code that uses it. Only for values
// used at run time: not array sizes, not case labels, not static initialisers.

#ifndef __P_TUNE__
#define __P_TUNE__

#ifdef DOOM_TUNE
int P_Tune (const char* name, int value);
// Told each number that differs from the source (for -derplog): now, and on each change.
void P_TuneWatch (void (*told) (const char* name, int value));
#define TUNE(name, value) P_Tune (#name, (value))
#else
#define TUNE(name, value) (value)
#endif

#endif
