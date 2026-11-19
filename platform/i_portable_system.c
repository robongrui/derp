/* Common C11 system interface for the portable and legacy Linux targets. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* The engine supplies fixed-width limits with these historical names. */
#undef MAXCHAR
#undef MAXSHORT
#undef MAXINT
#undef MAXLONG
#undef MINCHAR
#undef MINSHORT
#undef MININT
#undef MINLONG
#else
#include <errno.h>
#include <time.h>
#endif

#include "doomdef.h"
#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"
#include "m_misc.h"
#include "m_argv.h"
#include "d_net.h"
#include "g_game.h"
#include "game_profile.h"
#include "audio/sound_options.h"
#ifdef DOOM_NATIVE
#include <SDL3/SDL.h>
#endif

int mb_used = 16;
static ticcmd_t emptycmd;

static uint64_t (*audioClock)(void);
static uint64_t audioElapsed;

void I_SetAudioTiming(uint64_t (*clock)(void))
{
    audioClock = clock;
    audioElapsed = 0;
}

uint64_t I_BeginAudioTiming(void) { return audioClock ? audioClock() : 0; }

void I_EndAudioTiming(uint64_t start)
{
    if (audioClock) audioElapsed += audioClock() - start;
}

uint64_t I_TakeAudioTiming(void)
{
    uint64_t elapsed = audioElapsed;
    audioElapsed = 0;
    return elapsed;
}

ticcmd_t *I_BaseTiccmd(void) { return &emptycmd; }
int I_GetHeapSize(void) { return mb_used * 1024 * 1024; }

byte *I_ZoneBase(int *size)
{
    byte *memory;
    if (mb_used < 1 || mb_used > INT_MAX / (1024 * 1024))
        I_Error("Invalid zone size");
    *size = I_GetHeapSize();
    memory = malloc((size_t)*size);
    if (!memory)
        I_Error("Could not allocate the DOOM memory zone");
    return memory;
}

/* Game seconds per real second: slow motion (slow_motion.c). */
double I_TimeScale = 1;
/* A test player's fast clock (tests/derp_ai_probe.c): apart from slow motion, which eases its own back. */
double I_TestSpeed = 1;
#ifdef DOOM_DEVTOOLS
/* Local dev tools (devtools/) may run the game's clock slow as well. */
extern double devTimeScale;
#endif

/* Whether the game may drop tics it fell behind by after a hitch (d_net.c).
   The test drivers count on every tic, and a test player's fast clock runs
   many a frame on purpose. */
int I_MayDropTics(void)
{
#ifdef DOOM_TEST_DRIVER
    return 0;
#else
    return I_TestSpeed == 1;
#endif
}

/* Nanoseconds since the first call, on the game's clock. */
static uint64_t elapsedNanoseconds(void)
{
    uint64_t elapsed;
#ifdef _WIN32
    static LARGE_INTEGER frequency, base;
    LARGE_INTEGER now;
    if (!frequency.QuadPart)
    {
        if (!QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&base))
            I_Error("Could not initialize the monotonic clock");
    }
    if (!QueryPerformanceCounter(&now))
        I_Error("Could not read the monotonic clock");
    uint64_t counts = (uint64_t)(now.QuadPart - base.QuadPart);
    elapsed = (counts / frequency.QuadPart) * UINT64_C(1000000000)
            + (counts % frequency.QuadPart) * UINT64_C(1000000000) / frequency.QuadPart;
#else
    static struct timespec base;
    static boolean initialized;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        I_Error("Could not read the monotonic clock");
    if (!initialized)
    {
        base = now;
        initialized = true;
    }
    int64_t seconds = (int64_t)now.tv_sec - base.tv_sec;
    int64_t nanoseconds = (int64_t)now.tv_nsec - base.tv_nsec;
    if (nanoseconds < 0)
    {
        --seconds;
        nanoseconds += INT64_C(1000000000);
    }
    elapsed = (uint64_t)seconds * UINT64_C(1000000000) + (uint64_t)nanoseconds;
#endif
    {
        /* Summed a call at a time, so changing the scale never jumps the clock. */
        static uint64_t realBefore, scaled;
        double scale = I_TimeScale * I_TestSpeed;
#ifdef DOOM_DEVTOOLS
        scale *= devTimeScale;
#endif
        scaled += (uint64_t)((double)(elapsed - realBefore) * scale);
        realBefore = elapsed;
        elapsed = scaled;
    }
    return elapsed;
}

int I_GetTime(void)
{
    uint64_t elapsed = elapsedNanoseconds();
    uint64_t ticks = (elapsed / UINT64_C(1000000000)) * TICRATE
                   + (elapsed % UINT64_C(1000000000)) * TICRATE / UINT64_C(1000000000);
    return (int)(ticks & INT32_MAX);
}

/* The same clock with the time since its last tic as a fraction: the 3D
   renderer blends the world between tics by it. */
double I_GetTimeExact(void)
{
    uint64_t elapsed = elapsedNanoseconds();
    return (double)((elapsed / UINT64_C(1000000000)) * TICRATE)
         + (double)((elapsed % UINT64_C(1000000000)) * TICRATE) / 1e9;
}

void I_WaitVBL(int count)
{
    if (count <= 0)
        return;
#ifdef _WIN32
    Sleep((DWORD)((uint64_t)count * 1000 / 70));
#else
    struct timespec delay = {
        count / 70,
        (long)((int64_t)(count % 70) * INT64_C(1000000000) / 70)
    };
    while (nanosleep(&delay, &delay) && errno == EINTR) { }
#endif
}

void I_Init(void) { I_InitSound(); }
void I_BeginRead(void) { }
const char *I_ProgramFile(const char *name)
{
#ifdef DOOM_NATIVE
    static char path[4096];
    const char *base = SDL_GetBasePath();
    FILE *file;
    if (!base) return NULL;
    snprintf(path, sizeof path, "%s%s", base, name);
    file = fopen(path, "rb");
    if (!file) return NULL;
    fclose(file);
    return path;
#else
    (void)name;
    return NULL;
#endif
}
void I_EndRead(void) { }
void I_Tactile(int on, int off, int total)
{
    (void)on; (void)off; (void)total;
}

byte *I_AllocLow(int length)
{
    byte *memory;
    if (length < 0)
        I_Error("Invalid allocation size");
    memory = calloc((size_t)length, 1);
    if (!memory)
        I_Error("Could not allocate memory");
    return memory;
}

void I_Quit(void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
    exit(EXIT_SUCCESS);
}

void I_Error(char *error, ...)
{
    va_list args;
    va_start(args, error);
#ifdef DOOM_NATIVE
    char message[1024];
    va_list display_args;
    va_copy(display_args, args);
    vsnprintf(message, sizeof(message), error, display_args);
    va_end(display_args);
#endif
    fputs("Error: ", stderr);
    vfprintf(stderr, error, args);
    fputc('\n', stderr);
    va_end(args);
    I_ShutdownSound();
    I_ShutdownMusic();
#ifdef DOOM_NATIVE
    /* No window to show it over in the tests (the dummy video driver), and
       none for a render check's hidden window either: the message is on
       stderr, and a box would wait on the desktop of whoever is at it. */
    const char *video = getenv("SDL_VIDEODRIVER");
    if ((!video || strcmp(video, "dummy")) && !M_CheckParm("-rendercheck"))
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Game_Title(), message, NULL);
#endif
    I_ShutdownGraphics();
    exit(EXIT_FAILURE);
}
