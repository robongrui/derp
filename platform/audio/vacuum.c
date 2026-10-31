/* Sound without air (vacuum.h). */
#include <math.h>
#include "doomstat.h"
#include "p_local.h"
#include "../game_profile.h"
#include "acoustic_graph.h"
#include "occlusion.h"
#include "vacuum.h"

/* Your own steps, shots and voice come through the suit and helmet: this
   loud, this dull, with no air outside. */
#define SUIT_GAIN 0.55f
#define SUIT_HZ 350.0f
/* A sound this close on the same floor still thuds up through your boots,
   faint and low, as long as you stand on it. */
#define FLOOR_REACH 192.0f
#define FLOOR_GAIN 0.3f
#define FLOOR_HZ 220.0f

static float air(int sector) { return Game_Air(sector); }

void Vacuum_Follow(void) { Acoustic_SetAir(Game_Phobos() ? air : NULL); }

float Vacuum_Carry(fixed_t x, fixed_t y, int own, float *cutoff)
{
    *cutoff = OCCLUSION_OPEN_HZ;
    const mobj_t *listener = players[consoleplayer].mo;
    if (!Game_Phobos() || gamestate != GS_LEVEL || !listener || !listener->subsector) return 1;
    float here = Game_Air((int)(listener->subsector->sector - sectors));
    float carried = here;
    if (!own) carried = fminf(carried, Game_Air((int)(R_PointInSubsector(x, y)->sector - sectors)));
    if (carried >= 1) return 1;
    /* Thinner air carries less and duller; none, only what touches you. */
    float touch = 0, touch_hz = OCCLUSION_OPEN_HZ;
    if (own) { touch = SUIT_GAIN; touch_hz = SUIT_HZ; }
    else if (listener->z <= listener->floorz)
    {
        float d = hypotf((x - listener->x) / (float)FRACUNIT, (y - listener->y) / (float)FRACUNIT);
        if (d < FLOOR_REACH) { touch = FLOOR_GAIN * (1 - d / FLOOR_REACH); touch_hz = FLOOR_HZ; }
    }
    float gain = carried + (1 - carried) * touch;
    if (gain <= 0) return 0;
    /* The air's share keeps its treble, the touch's is low. */
    float share = carried / gain;
    *cutoff = expf(share * logf(OCCLUSION_OPEN_HZ) + (1 - share) * logf(touch_hz));
    return gain;
}
