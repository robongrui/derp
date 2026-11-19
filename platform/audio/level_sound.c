/* Sound through the whole level. See level_sound.h. */
#include <math.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "acoustic_graph.h"
#include "level_sound.h"
#include "occlusion.h"
#include "room.h"
#include "room_records.h"
#include "../game_profile.h"

#define NEAR 160.0f          /* closer than this a sound is at full strength */
#define VANILLA 1200.0f      /* DOOM's clipping distance */
#define PAST_DB 0.002f       /* past it, this many dB a unit more than spreading takes (air, clutter) */
#define FAINTEST_DB 50.0f    /* below this nothing is heard */
#define SPEED 10700.0f       /* sound, map units per second (room.c) */
#define WAITING 16           /* far sounds queued at most */
#define AGAIN_TICS 12        /* the same far sound no sooner than this after the last */
#define STIR_SOONEST 6       /* seconds between sleepers stirring, at the least */
#define STIR_LATEST 20       /* and at the most */
#define STIR_NEAR 384.0f     /* not right next to the listener: that one would wake */
#define STIR_FAR 4000.0f
#define STIR_LEVEL 0.7f      /* a sleeper only murmurs */
#define STIR_HEARD_DB 47.0f  /* the loudest of a few, if it comes through louder than this below full */
#define STIR_TRIES 8

static int authored, on;

void LevelSound_Set(int value) { authored = value != 0; }
int LevelSound_On(void) { return on; }

int LevelSound_Follow(void)
{
    int now = authored && gamestate == GS_LEVEL && Game_HasEffect(GAME_EFFECT_AUTHORED_EPISODES) && Game_StockMap()
              && !Game_InLibrary() && !Game_Phobos();
    if (now != on)
    {
        on = now;
        Acoustic_SetFar(on);
        Occlusion_SetFar(on);
        Room_GlassStops(on);
        RoomRecords_Reset();
    }
    return on;
}

float LevelSound_Gain(float length, float loss, float reach)
{
    float d = length / (reach > 1 ? reach : 1);
    float gain = d < NEAR ? 1 : NEAR / d;
    float db = loss + (d > VANILLA ? (d - VANILLA) * PAST_DB : 0);
    gain *= powf(10, -db / 20);
    return gain < powf(10, -FAINTEST_DB / 20) ? 0 : gain;
}

static far_sound_t queued[WAITING];
static int waiting_count, last_id = -1, last_tic, level_seen = -1;

void LevelSound_Queue(int id, float x, float y, float z, float level, float reach, float length)
{
    if (level_seen != gamemap + 100 * gameepisode) { level_seen = gamemap + 100 * gameepisode; waiting_count = 0; }
    if (!on || waiting_count == WAITING) return;
    if (id == last_id && gametic >= last_tic && gametic - last_tic < AGAIN_TICS) return;
    last_id = id; last_tic = gametic;
    queued[waiting_count++] = (far_sound_t){ id, x, y, z, level, reach, gametic + (int)(length / SPEED * TICRATE + 0.5f) };
}

int LevelSound_Due(far_sound_t *out)
{
    if (!on) { waiting_count = 0; return 0; }
    for (int i = 0; i < waiting_count; ++i)
    {
        if (queued[i].due > gametic && queued[i].due - gametic < 10 * TICRATE) continue;
        *out = queued[i];
        queued[i] = queued[--waiting_count];
        return 1;
    }
    return 0;
}

static unsigned stir_seed = 0x5eed1e55u;
static int stir_due = -1, stir_level = -1;
static float stir_last_x, stir_last_y;
static unsigned stir_next(void) { stir_seed = stir_seed * 1103515245u + 12345u; return stir_seed >> 8; }

int LevelSound_Stir(float x, float y, far_sound_t *out)
{
    int level = gamemap + 100 * gameepisode;
    if (stir_level != level || stir_due < 0 || gametic < stir_due - STIR_LATEST * TICRATE)
    {
        stir_level = level;
        stir_due = gametic + (STIR_SOONEST + (int)(stir_next() % (STIR_LATEST - STIR_SOONEST))) * TICRATE;
    }
    if (!on || gametic < stir_due) return 0;
    stir_due = gametic + (STIR_SOONEST + (int)(stir_next() % (STIR_LATEST - STIR_SOONEST))) * TICRATE;
    /* The loudest of a few: behind shut doors most sleepers are all but lost. */
    game_sleeper sleeper, candidate;
    float loudest = 0;
    for (int tries = 0; tries < STIR_TRIES; ++tries)
    {
        if (!Game_Sleeper(x, y, STIR_NEAR, STIR_FAR, stir_next(), &candidate)) return 0;
        if (fabsf(candidate.x - stir_last_x) + fabsf(candidate.y - stir_last_y) <= 1) continue;
        occlusion_path_t path;
        Occlusion_Path((fixed_t)(candidate.x * FRACUNIT), (fixed_t)(candidate.y * FRACUNIT), (fixed_t)(candidate.z * FRACUNIT), &path);
        float heard = LevelSound_Gain(path.length, path.loss, 1) * STIR_LEVEL;
        if (heard > loudest) { loudest = heard; sleeper = candidate; }
    }
    if (loudest <= powf(10, -STIR_HEARD_DB / 20)) return 0;
    stir_last_x = sleeper.x; stir_last_y = sleeper.y;
    *out =(far_sound_t){ sleeper.sound, sleeper.x, sleeper.y, sleeper.z, STIR_LEVEL, 1, gametic };
    return 1;
}
