/* DOOM's answers to platform/game_profile.h. */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "p_local.h"
#include "p_phobos.h"
#include "p_derp.h"
#include "p_derptiming.h"
#include "p_hang.h"
#include "p_tactics.h"
#include "p_gunwalk.h"
#include "p_senses.h"
#include "p_weather.h"
#include "p_library.h"
#include "p_bullets.h"
#include "p_rounds.h"
#include "p_swept.h"
#include "p_kick.h"
#include "p_moves.h"
#include "p_focus.h"
#include "p_barks.h"
#include "../../derp_info.h"
#include "m_argv.h"
#include "sounds.h"
#include "i_sound.h"
#include "s_sound.h"
#include "w_wad.h"
#include "../../game_profile.h"
#include "../../wad_probe.h"

void T_FireFlicker(fireflicker_t *flick);

const char *Game_Title(void) { return "DOOM"; }
const char *Game_ConfigFile(void) { return "doom.cfg"; }
int Game_WadKind(void) { return DOOM_WAD_DOOM; }
const char *Game_ConfigDefaults(void)
{
    return "key_up 119\nkey_down 115\nkey_strafeleft 97\nkey_straferight 100\n"
           "key_use 32\nkey_fire 157\nmouse_sensitivity 5\n"
           "sfx_volume 10\nmusic_volume 7\nsnd_channels 16\n";
}

/* The bindings: g_binds.h's variables, as F4's Bindings page lists them. */
#define BIND(name, title, section, role, key, key2, pad) extern int key_##name, key2_##name, pad_##name;
#define PRESS(name, title, section, key, pad) extern int pad_##name;
#include "g_binds.h"
#undef BIND
#undef PRESS
static const game_action actions[] = {
#define BIND(name, title, section, role, key, key2, pad) \
    {title, section, {&key_##name, &key2_##name, &pad_##name}, {key, key2, pad}, GAME_ACTION_##role, KEY_NONE},
#define PRESS(name, title, section, key, pad) \
    {title, section, {NULL, NULL, &pad_##name}, {KEY_NONE, KEY_NONE, pad}, GAME_ACTION_PRESS, key},
#include "g_binds.h"
#undef BIND
#undef PRESS
};
const game_action *Game_Actions(int *count)
{
    *count = (int)(sizeof(actions) / sizeof(actions[0]));
    return actions;
}

int M_PadMode(void);
extern boolean automapactive;
game_pad_mode Game_PadMode(void)
{
    int menu = M_PadMode();
    if (menu) return (game_pad_mode)menu;
    return gamestate == GS_LEVEL && automapactive ? GAME_PAD_AUTOMAP : GAME_PAD_PLAY;
}

/* The player's own guns, saw, pain, landing and death. A gun kicks once per
   shot (the chaingun twice, its sound each time); the saw's sounds come
   every few tics while it runs, so their holds join into one buzz. The BFG's
   sound starts the charge: the rumble swells to the shot. */
int Game_SoundRumble(int id, game_rumble *r)
{
    static const struct { int id; game_rumble rumble; } tuned[] = {
        {sfx_pistol,  {0.30f, 0.30f, 0.45f, 0, 0.02f, 0.09f}},
        {sfx_shotgn,  {0.80f, 0.45f, 0.80f, 0, 0.04f, 0.24f}},
        {sfx_dshtgn,  {1.00f, 0.65f, 1.00f, 0, 0.07f, 0.38f}},
        {sfx_rlaunc,  {0.65f, 0.20f, 0.55f, 0.02f, 0.10f, 0.30f}},
        {sfx_plasma,  {0.08f, 0.40f, 0.25f, 0, 0.02f, 0.05f}},
        {sfx_bfg,     {1.00f, 0.55f, 1.00f, 0.75f, 0.10f, 0.60f}},
        {sfx_punch,   {0.55f, 0.25f, 0.30f, 0, 0.03f, 0.12f}},
        {sfx_sawup,   {0.30f, 0.50f, 0, 0.05f, 0.25f, 0.20f}},
        {sfx_sawidl,  {0.10f, 0.30f, 0, 0, 0.13f, 0.04f}},
        {sfx_sawful,  {0.25f, 0.65f, 0.25f, 0, 0.13f, 0.04f}},
        {sfx_sawhit,  {0.55f, 0.90f, 0.45f, 0, 0.13f, 0.05f}},
        {sfx_oof,     {0.60f, 0.15f, 0, 0, 0.03f, 0.25f}},
        {sfx_plpain,  {0.85f, 0.55f, 0, 0, 0.05f, 0.55f}},
        {sfx_pldeth,  {1.00f, 0.45f, 0, 0, 0.20f, 1.60f}},
        {sfx_pdiehi,  {1.00f, 0.60f, 0, 0, 0.25f, 1.80f}},
    };
    for (unsigned i = 0; i < sizeof(tuned) / sizeof(tuned[0]); ++i)
        if (tuned[i].id == id) { *r = tuned[i].rumble; return 1; }
    return 0;
}

int Game_ThinkerIsMobj(const struct thinker_s *thinker)
{
    return thinker->function.acp1 == (actionf_p1)P_MobjThinker;
}

void Game_LightSwings(game_swing_visit visit, void *data)
{
    for (thinker_t *th = thinkercap.next; th && th != &thinkercap; th = th->next)
    {
        const sector_t *sector = NULL;
        int low = 0, high = 0, smooth = 0;
        if (th->function.acp1 == (actionf_p1)T_LightFlash)
        { lightflash_t *l = (lightflash_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function.acp1 == (actionf_p1)T_StrobeFlash)
        { strobe_t *l = (strobe_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; }
        else if (th->function.acp1 == (actionf_p1)T_Glow)
        { glow_t *l = (glow_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; smooth = 1; }
        else if (th->function.acp1 == (actionf_p1)T_FireFlicker)
        { fireflicker_t *l = (fireflicker_t *)th; sector = l->sector; low = l->minlight; high = l->maxlight; smooth = 1; }
        if (sector && high > low) visit((int)(sector - sectors), low, high, smooth, data);
    }
}

game_thing_role Game_ThingRole(int type)
{
    switch (type)
    {
    case MT_ROCKET: return GAME_THING_ROCKET;
    case MT_BFG: return GAME_THING_BIG_SHOT;
    case MT_BLOOD: return GAME_THING_BLOOD;
    case MT_PUFF: return GAME_THING_PUFF;
    case MT_TFOG: case MT_IFOG: return GAME_THING_TELEPORT_FOG;
    case MT_BARREL: return GAME_THING_BARREL;
    case MT_SKULL: return GAME_THING_UNCOUNTED_MONSTER;
    case MT_PLASMA: case MT_TROOPSHOT: case MT_HEADSHOT: case MT_BRUISERSHOT: case MT_ARACHPLAZ: case MT_FATSHOT:
        return GAME_THING_BURN_SHOT;
    default: return GAME_THING_OTHER;
    }
}

/* Authored: the Cacodemon bleeds blue, the Barons and Hell Knights green, a
   Lost Soul is bone and fire with nothing to bleed. */
game_blood_tint Game_BloodTint(int type)
{
    switch (type)
    {
    case MT_HEAD: return GAME_BLOOD_BLUE;
    case MT_BRUISER: case MT_KNIGHT: return GAME_BLOOD_GREEN;
    case MT_SKULL: return GAME_BLOOD_NONE;
    default: return GAME_BLOOD_RED;
    }
}

int Game_Explosions(void) { return 1; }

int Game_BlastFrame(const struct mobj_s *thing)
{
    int state;
    if (thing->type != MT_BARREL || !thing->state) return -1;
    state = (int)(thing->state - states);
    return state >= S_BEXP && state <= S_BEXP5 ? state - S_BEXP : -1;
}

void Game_MuzzleFlash(int weapon, float *radius, float *strength, int *flashstate)
{
    if (weapon == wp_shotgun || weapon == wp_supershotgun) { *radius = 448; *strength = 2.4f; }
    else if (weapon == wp_missile || weapon == wp_bfg) { *radius = 480; *strength = 2.6f; }
    else if (weapon == wp_plasma) { *radius = 320; *strength = 1.6f; }
    *flashstate = weapon >= 0 && weapon < NUMWEAPONS ? (int)weaponinfo[weapon].flashstate : -1;
}

int Game_MuzzleFire(int weapon)
{
    if (weapon == wp_pistol) return 1;
    if (weapon == wp_shotgun || weapon == wp_supershotgun) return 2;
    if (weapon == wp_chaingun) return 3;
    return 0;
}

int Game_MonsterBooted(const struct mobj_s *mo)
{
    return P_DerpAI() && P_DerpSoldier(mo->type);
}

int Game_MonsterClawed(const struct mobj_s *mo)
{
    return P_DerpAI() && mo->type == MT_TROOP;
}

int Game_WeaponHeft(int weapon)
{
    if (weapon == wp_missile || weapon == wp_bfg) return 2;
    if (weapon == wp_shotgun || weapon == wp_supershotgun || weapon == wp_chaingun || weapon == wp_plasma) return 1;
    return 0;
}

/* The skin suit's skeletons: the imp, the zombieman, the shotgun guy, the chaingunner (hand rigged one by one). */
game_skeleton Game_Skeleton(int type)
{
    switch (type)
    {
    case MT_TROOP: case MT_POSSESSED: case MT_SHOTGUY: case MT_CHAINGUY:
    case MT_BRUISER: case MT_KNIGHT: case MT_CYBORG: case MT_UNDEAD: case MT_VILE: case MT_WOLFSS: case MT_FATSO:
        return GAME_SKELETON_HUMANOID;
    default:
        return GAME_SKELETON_NONE;
    }
}

/* The hand rigs the user has finished (a rig in progress is no example for the fitter). */
int Game_BodyMaterials(int type) { return type == MT_UNDEAD; }
int Game_BulletCount(void) { return P_BulletCount(); }
int Game_BulletStretch(int index, float from[3], float to[3])
{
    fixed_t a[3], b[3];
    int i;
    if (!P_BulletStretch(index, a, b)) return 0;
    for (i = 0; i < 3; ++i) { from[i] = a[i]/(float)FRACUNIT; to[i] = b[i]/(float)FRACUNIT; }
    return 1;
}
int Game_HeadKill(const struct mobj_s *thing)
{
    mobj_t *me = players[consoleplayer].mo;
    return thing && me && P_BulletsTravel() && P_DerpSoldier(thing->type)
        && thing->headtic >= leveltime - 1 && P_RoundOf(me) == ROUND_PISTOL;
}
int Game_BodyBall(int type) { return type == MT_HEAD ? 100 : type == MT_PAIN ? 140 : 0; }
int Game_DeathSpawns(int type) { return type == MT_PAIN ? 32 : 0; }
int Game_BodyFat(int type) { return type == MT_FATSO; }
int Game_MouthOpen(const struct mobj_s *thing)
{
    if (!thing) return 0;
    if (thing->type == MT_PAIN) return thing->state >= &states[S_PAIN_ATK1] && thing->state <= &states[S_PAIN_ATK4];
    return thing->type == MT_HEAD && thing->state >= &states[S_HEAD_ATK1] && thing->state <= &states[S_HEAD_ATK3];
}

int Game_SkeletonFinished(int type)
{
    return type == MT_TROOP || type == MT_POSSESSED || type == MT_SHOTGUY;
}

int Game_ShootsHitscan(const struct mobj_s *monster)
{
    return monster && (monster->type == MT_POSSESSED || monster->type == MT_SHOTGUY || monster->type == MT_CHAINGUY || monster->type == MT_WOLFSS || monster->type == MT_SPIDER);
}

int Game_MeleeWeapon(const struct mobj_s *shooter)
{
    if (!shooter || !shooter->player) return 0;
    return shooter->player->readyweapon == wp_fist ? 1 : shooter->player->readyweapon == wp_chainsaw ? 2 : 0;
}

int Game_LookPitch(float *pitch) { (void)pitch; return 0; }
void Game_Look(float delta) { (void)delta; }

int Game_Sprite(game_sprite_role role)
{
    switch (role)
    {
    case GAME_SPRITE_BLOOD: return SPR_BLUD;
    case GAME_SPRITE_PUFF: return SPR_PUFF;
    case GAME_SPRITE_EXPLOSION: return SPR_BEXP;
    case GAME_SPRITE_TORCH: return -1;
    }
    return -1;
}

int Game_HasEffect(game_effect effect) { (void)effect; return 1; }
void Game_SetRandomPitch(int enabled) { snd_randompitch = enabled; }
void Game_SetMonsterVoices(int enabled) { P_SetMonsterVoices(enabled != 0); }

/* On Phobos' night side Mars is the sky light: 42 degrees of sunlit planet,
   some 3% of full sunlight (ten thousand full moons), and most of the light
   outside takes its colour. */
void Game_Sun(game_sun *sun)
{
    *sun = (game_sun){ 0, 0.3f, 0, P_Phobos () ? 0.85f : 0.3f, 0 };
}

void Game_SetPhobos(int on) { phobosphysics = on || M_CheckParm ("-phobos"); }
void Game_SetAuthored(int on) { derpauthored = on != 0; }
void Game_SetAiTiming(uint64_t (*clock)(void)) { P_DerpTimingSet(clock); }
game_ai_timing Game_TakeAiTiming(void)
{
    uint64_t times[DERP_TIMING_COUNT];
    P_DerpTimingTake(times);
    return (game_ai_timing){times[DERP_TIMING_NAV], times[DERP_TIMING_SENSE],
                            times[DERP_TIMING_BRAIN], times[DERP_TIMING_COORDINATION]};
}
int Game_AiActive(void) { return gamestate == GS_LEVEL && P_DerpAI(); }
void Game_SetWeather(int choice) { weatherchoice = choice; }
extern unsigned climateseed;
unsigned Game_ClimateSeed(void) { return climateseed; }
int Game_NukageRain(void) { return P_NukageRain (); }
void Game_SetPhobosRules(int player, int enemies, int air, int recoil)
{ phobosplayer = player; phobosenemies = enemies; phobosair = air; phobosrecoil = recoil != 0; }
int Game_Phobos(void) { return P_Phobos (); }
float Game_PhobosGravity(void) { return DerpInfo_Map.gravity >= 0 ? P_PhobosPull () : 1.49e-4f; }
void Game_WorldShift(float *dx, float *dy, float *dz) { P_LibraryShift (dx, dy, dz); }
int Game_Carried(const struct mobj_s *thing) { return P_LibraryCarried (thing); }
int Game_WorldWraps(void) { return P_Library (); }
int Game_WorldRenewed(void) { return P_LibraryRenewed (); }
int Game_FloorsAbove(void) { return P_LibraryFloorsAbove (); }
int Game_FloorsBelow(void) { return P_LibraryFloorsBelow (); }
int Game_Storey(int sector, float *storey, float *bottom)
{
    fixed_t height, low;
    if (!P_LibraryStorey (sector, &height, &low))
        return 0;
    *storey = height/(float)FRACUNIT;
    *bottom = low/(float)FRACUNIT;
    return 1;
}
boolean M_LibraryBlack(void);
int Game_LibraryEyes(float *lids, float *black)
{
    /* The questions and the welcome (m_library.c) are on black. */
    if (M_LibraryBlack ()) { *lids = 0; *black = 1; return 1; }
    return P_LibraryEyes (lids, black);
}
int Game_LibraryYawns(void) { return P_LibraryYawns (); }
const char *Game_LibraryNote(void) { return P_LibraryNote (); }
int Game_LibraryAim(float corners[4][3], int *read)
{
    boolean seen = false, aimed = P_LibraryAim (corners, &seen);
    *read = seen;
    return aimed;
}
int M_KioskPage(char *lines, char *marks);
int Game_LibraryPage(char *lines, char *marks)
{
    int kiosk = M_KioskPage (lines, marks);   /* the kiosk's menu, shown as a page */
    return kiosk ? kiosk : P_LibraryPage (lines, marks);
}
const char *Game_LibrarySays(const struct mobj_s *thing, double seconds, float *shown) { return P_LibrarySays ((const mobj_t *)thing, seconds, shown); }
float Game_LibraryRinging(void) { return P_LibraryRinging (); }
int Game_LibraryKnocks(void) { return P_LibraryKnocks (); }
float Game_LibraryFallSpeed(void) { return P_LibraryFallSpeed (); }
int Game_InLibrary(void) { return P_Library (); }
int Game_StockMap(void) { return P_StockMap (gameepisode, gamemap); }
/* Asleep: a monster that has no one to go after yet. */
static int sleeper (const mobj_t *mo, float x, float y, float near, float far)
{
    if (!(mo->flags & MF_COUNTKILL) || mo->health <= 0 || mo->target || mo->info->activesound <= 0)
	return 0;
    float d = P_AproxDistance (mo->x - (fixed_t)(x * FRACUNIT), mo->y - (fixed_t)(y * FRACUNIT)) / (float)FRACUNIT;
    return d >= near && d <= far;
}
int Game_Sleeper(float x, float y, float near, float far, unsigned pick, game_sleeper *out)
{
    thinker_t *th;
    unsigned count = 0;
    if (gamestate != GS_LEVEL)
	return 0;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
	if (th->function.acp1 == (actionf_p1)P_MobjThinker && sleeper ((mobj_t *)th, x, y, near, far))
	    ++count;
    if (!count)
	return 0;
    pick %= count;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t *mo = (mobj_t *)th;
	if (th->function.acp1 != (actionf_p1)P_MobjThinker || !sleeper (mo, x, y, near, far) || pick--)
	    continue;
	out->x = mo->x / (float)FRACUNIT; out->y = mo->y / (float)FRACUNIT; out->z = mo->z / (float)FRACUNIT;
	out->sound = mo->info->activesound;
	return 1;
    }
    return 0;
}
float Game_LibraryChasm(float *side) { return P_LibraryChasm (side); }
float Game_LibraryTired(void) { return P_LibraryTired (); }
int Game_LibraryFall(float speed[3]) { return P_LibraryFall (&speed[0], &speed[1], &speed[2]); }
int Game_ThingAsleep(const struct mobj_s *thing) { return P_LibraryAsleep (thing); }
int Game_LibraryFigures(game_figure *out, int most, double seconds)
{
    static library_figure_t	figures[1024];
    int				i, n = P_LibraryFigures (figures, most < 1024 ? most : 1024, seconds);

    for (i = 0; i < n; i++)
    {
	out[i].x = figures[i].x;
	out[i].y = figures[i].y;
	out[i].angle = figures[i].angle;
	out[i].sprite = figures[i].sprite;
	out[i].frame = figures[i].frame;
	out[i].type = figures[i].type;
	out[i].storey = figures[i].storey;
    }
    return n;
}
float Game_Air(int sector) { return sector >= 0 && sector < numsectors ? P_SectorAir (&sectors[sector]) : 1; }
void Game_Glass(int line, int whole) { P_PhobosGlass (line, whole); }
int Game_Vents(game_vent *vents, int most) { return P_AirVents ((phobos_vent_t *)vents, most); }
int Game_Adrift(const struct mobj_s *thing) { return P_Adrift ((mobj_t *)thing); }
float Game_Draft(const struct mobj_s *thing, float way[3]) { return P_Draft ((mobj_t *)thing, way); }
int Game_Tumble(const struct mobj_s *thing, float fraction, float up[3]) { return P_BodyUp ((mobj_t *)thing, fraction, up); }
int Game_Kneeling(const struct mobj_s *thing) { return P_DerpKneeling ((mobj_t *)thing); }
void Game_KneelForTest(struct mobj_s *thing)
{
    mobj_t *mo = (mobj_t *)thing;
    if (mo->kneeltic)
	return;
    mo->kneeltic = leveltime + TICRATE*3600;
    mo->height = mo->info->height/2;
}
float Game_Crouch(const struct mobj_s *thing) {
    /* An imp's jump, or a kicked man down from his kip-up (p_swept.c). */
    float jump = P_DerpCrouch (thing)/65536.0f, kip = P_SweptCrouch ((const mobj_t *)thing, 1);
    return jump > kip ? jump : kip;
}
float Game_Hang(const struct mobj_s *thing, float fraction) { return P_HangRoll ((const mobj_t *)thing, fraction); }
float Game_Swept(const struct mobj_s *thing, float fraction, float *dx, float *dy) { return P_SweptShare ((const mobj_t *)thing, fraction, dx, dy); }
float Game_SweptDead(const struct mobj_s *thing, float fraction, float *dx, float *dy, float *up) { *up = ((const mobj_t *)thing)->momz/65536.0f; return P_SweptTilt ((const mobj_t *)thing, fraction, dx, dy); }
int Game_Sliding(const struct mobj_s *thing) { return P_SlidingBody ((const mobj_t *)thing); }
float Game_SlideView(int player, float fraction) { return P_SlideView (&players[player], fraction); }
int Game_Kick(int player, int *frame, float *dip) { return P_KickShown (&players[player], frame, dip); }
int Game_HeadStrikes(float x, float y, float z, float radius, int player)
{
    return P_HeadStrikes ((fixed_t)(x*FRACUNIT), (fixed_t)(y*FRACUNIT), (fixed_t)(z*FRACUNIT), (fixed_t)(radius*FRACUNIT), player);
}
unsigned Game_SweptFacing(const struct mobj_s *thing) { const mobj_t *mo = (const mobj_t *)thing; return P_SweptFacing (mo, mo->angle); }
float Game_SweptRoll(const struct mobj_s *thing) { return P_SweptRoll ((const mobj_t *)thing); }
int Game_DrawAngle(const struct mobj_s *thing, unsigned *angle) {
    const mobj_t *mo = (const mobj_t *)thing;
    if (mo->health <= 0) return 0;
    *angle = P_DerpDrawAngle (mo);
    return 1;
}
int Game_WalkingShot(const struct mobj_s *thing, unsigned *heading) {
    const mobj_t *mo = (const mobj_t *)thing;
    if (!P_GunWalking (mo) || mo->movedir >= 8) return 0;
    *heading = (unsigned)mo->movedir << 29;
    return 1;
}
int Game_Lean(const struct mobj_s *thing, float *dx, float *dy)
{
    fixed_t x, y;
    if (!P_DerpLean ((mobj_t *)thing, &x, &y) && !P_DerpMantleLean ((mobj_t *)thing, &x, &y)) return 0;
    *dx = x/65536.0f; *dy = y/65536.0f;
    return 1;
}
int Game_StrideLegs(const struct mobj_s *thing) { (void)thing; return P_DerpAI () && derpparts.strides; }
float Game_LookKick(void) { return P_LookKick (); }
int Game_ViewFlipped(void) { return P_ViewFlipped (); }
void Game_FlipView(void) { P_FlipView (); }
int Game_BootsUpside(void) { return P_BootsUpside (); }
void Game_BootsRolled(int over) { P_BootsRolled (over); }
/* Both the authored looks and Phobos are the first episode's: there, the map
   starts again as the new way to play builds it (its air, ice, light bake and
   moods), the player keeping what he carries as at a level change (keys and
   powers go, as when he leaves a map). DERP's monsters come and go with
   Authored on any of id's own maps: there too, as they spawn differently. */
void G_PlayerFinishLevel (int player);
int Game_ModeChanged(void)
{
    if (gamestate != GS_LEVEL || demoplayback || netgame)
        return 0;
    if ((gamemode == commercial || gameepisode != 1) && !P_DerpChanged ())
        return 0;
    if (players[consoleplayer].mo)
        G_PlayerFinishLevel (consoleplayer);
    gameaction = ga_loadlevel;
    return 1;
}

float Game_WaterClarity(void) { return 0; }

int Game_SurfaceGloss(void) { return 0; }

/* DOOM has the flashlight for dark rooms. */
int Game_HeroLight(void) { return 0; }

int Game_SectorWind(int special, float *east, float *north) { (void)special; *east = *north = 0; return 0; }

extern boolean menuactive, chat_on;
int Game_TakesText(void) { return menuactive || chat_on; }

/* DOOM's light amplification visor is a colormap, not a flame. */
int Game_Torch(float *flame) { *flame = 0; return 0; }

extern boolean d_wiping;
boolean M_DrawsBelow(int y);
int Game_StatusBarTop(void)
{
    if (gamestate != GS_LEVEL || !gametic || d_wiping) return 0;
    if (viewheight == SCREENHEIGHT && !automapactive) return 0;   /* no bar up */
    if (M_DrawsBelow(SCREENHEIGHT - 32)) return 0;
    return SCREENHEIGHT - 32;
}
const char *Game_BorderFlat(void) { return gamemode == commercial ? "GRNROCK" : "FLOOR7_2"; }

int Game_SlowRules(void) { return P_FocusRules(); }
int Game_SlowCosts(void) { return P_FocusCosts(); }
int Game_SlowLeft(void) { return P_FocusLeft() > 0; }
int Game_SlowAsked(void) { return P_FocusAsked(); }
void Game_SlowHand(int slow) { P_FocusHand(slow != 0); }
void Game_SlowClock(double scale) { P_FocusClock(scale); }
void Game_SlowFree(int free) { P_FocusFree = free != 0; }
int Game_SlowGlass(int *fill, int *most, int *flowing)
{
    if (!P_FocusRules() || gamestate != GS_LEVEL) return 0;
    *fill = P_FocusLeft();
    *most = FOCUS_MOST;
    *flowing = P_FocusFlowing();
    return 1;
}

/* Every lit decoration in DOOM is drawn fullbright. */
int Game_DecorationLight(int sprite) { (void)sprite; return 0; }

game_decoration Game_Decoration(int sprite)
{
    switch (sprite)
    {
    case SPR_COLU: case SPR_TLMP: case SPR_TLP2: return GAME_DECORATION_LAMP;
    case SPR_TBLU: case SPR_TGRN: case SPR_TRED: case SPR_FCAN: return GAME_DECORATION_TORCH;
    case SPR_SMBT: case SPR_SMGT: case SPR_SMRT: return GAME_DECORATION_SHORT_TORCH;
    case SPR_CBRA: return GAME_DECORATION_CANDLES;
    default: return GAME_DECORATION_NONE;
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    int lump;
    if (sfx->link) sfx = sfx->link;
    snprintf(name, sizeof(name), "ds%s", sfx->name);
    lump = W_CheckNumForName(name);
    return lump >= 0 ? lump : W_GetNumForName("dspistol");
}

void Game_SfxLoaded(int id, void *data)
{
    S_sfx[id].data = data;
}

/* Gunshots and blasts: a gunshot is a deafening crack up close and carries
   across a courtyard and beyond. The monsters' guns are the player's. */
float Game_SoundCarry(int id)
{
    switch (id)
    {
    case sfx_pistol: case sfx_shotgn: case sfx_dshtgn: case sfx_chgun: case sfx_plasma: case sfx_bfg:
    case sfx_rlaunc: case sfx_barexp: case sfx_rxplod: case sfx_firxpl:
        return 2.5f;
    default:
        return 1;
    }
}

static game_shot_traced shot_listener, shot_also;
static void shot_traced(mobj_t *shooter, mobj_t *hit, fixed_t x1, fixed_t y1, fixed_t z1,
                        fixed_t x2, fixed_t y2, fixed_t z2)
{
    const float unit = 1.0f / FRACUNIT;
    if (shot_listener) shot_listener(shooter, hit, x1 * unit, y1 * unit, z1 * unit, x2 * unit, y2 * unit, z2 * unit);
    if (shot_also) shot_also(shooter, hit, x1 * unit, y1 * unit, z1 * unit, x2 * unit, y2 * unit, z2 * unit);
}
void Game_TraceShots(game_shot_traced traced)
{
    shot_listener = traced;
    P_ShotTraced = shot_listener || shot_also ? shot_traced : NULL;
}
void Game_TraceShotsAlso(game_shot_traced traced)
{
    shot_also = traced;
    P_ShotTraced = shot_listener || shot_also ? shot_traced : NULL;
}

int Game_FontLump(int c)
{
    char name[9];
    snprintf(name, sizeof(name), "STCFN%03d", c);
    return W_CheckNumForName(name);
}

int Game_FontSpacing(void) { return 0; }

void Game_GiveAllKeys(void)
{
    for (int k = 0; k < NUMCARDS; ++k) players[consoleplayer].cards[k] = true;
}

int Game_MenuSound(game_menu_sound sound)
{
    switch (sound)
    {
    case GAME_MENU_MOVE: return sfx_pstop;
    case GAME_MENU_DENY: return sfx_oof;
    case GAME_MENU_CHANGE: return sfx_stnmov;
    case GAME_MENU_OPEN: return sfx_swtchn;
    case GAME_MENU_CLOSE: return sfx_swtchx;
    }
    return sfx_None;
}
int Game_StartLift(int tag)
{
    line_t line = {0};
    line.tag = (short)tag;
    return tag ? EV_DoPlat(&line, perpetualRaise, 0) : 0;
}
