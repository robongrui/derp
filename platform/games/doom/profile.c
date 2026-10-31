/* DOOM's answers to platform/game_profile.h. */
#include <stdio.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "p_local.h"
#include "p_phobos.h"
#include "p_weather.h"
#include "p_library.h"
#include "../../derp_info.h"
#include "m_argv.h"
#include "sounds.h"
#include "i_sound.h"
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
    case MT_TFOG: case MT_IFOG: return GAME_THING_TELEPORT_FOG;
    case MT_BARREL: return GAME_THING_BARREL;
    case MT_SKULL: return GAME_THING_UNCOUNTED_MONSTER;
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

int Game_WeaponHeft(int weapon)
{
    if (weapon == wp_missile || weapon == wp_bfg) return 2;
    if (weapon == wp_shotgun || weapon == wp_supershotgun || weapon == wp_chaingun || weapon == wp_plasma) return 1;
    return 0;
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

/* On Phobos' night side Mars is the sky light: 42 degrees of sunlit planet,
   some 3% of full sunlight (ten thousand full moons), and most of the light
   outside takes its colour. */
void Game_Sun(game_sun *sun)
{
    *sun = (game_sun){ 0, 0.3f, 0, P_Phobos () ? 0.85f : 0.3f, 0 };
}

void Game_SetPhobos(int on) { phobosphysics = on || M_CheckParm ("-phobos"); }
void Game_SetWeather(int choice) { weatherchoice = choice; }
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
float Game_LookKick(void) { return P_LookKick (); }
int Game_ViewFlipped(void) { return P_ViewFlipped (); }
void Game_FlipView(void) { P_FlipView (); }
int Game_BootsUpside(void) { return P_BootsUpside (); }
void Game_BootsRolled(int over) { P_BootsRolled (over); }
/* Both the authored looks and Phobos are the first episode's: there, the map
   starts again as the new way to play builds it (its air, ice, light bake and
   moods), the player keeping what he carries as at a level change (keys and
   powers go, as when he leaves a map). */
void G_PlayerFinishLevel (int player);
int Game_ModeChanged(void)
{
    if (gamestate != GS_LEVEL || demoplayback || netgame || gamemode == commercial || gameepisode != 1)
        return 0;
    if (players[consoleplayer].mo)
        G_PlayerFinishLevel (consoleplayer);
    gameaction = ga_loadlevel;
    return 1;
}

float Game_WaterClarity(void) { return 0; }

int Game_SurfaceGloss(void) { return 0; }

int Game_SectorWind(int special, float *east, float *north) { (void)special; *east = *north = 0; return 0; }

extern boolean menuactive, chat_on;
int Game_TakesText(void) { return menuactive || chat_on; }

/* DOOM's light amplification visor is a colormap, not a flame. */
int Game_Torch(float *flame) { *flame = 0; return 0; }

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

static game_shot_traced shot_listener;
static void shot_traced(mobj_t *shooter, mobj_t *hit, fixed_t x1, fixed_t y1, fixed_t z1,
                        fixed_t x2, fixed_t y2, fixed_t z2)
{
    const float unit = 1.0f / FRACUNIT;
    shot_listener(shooter, hit, x1 * unit, y1 * unit, z1 * unit, x2 * unit, y2 * unit, z2 * unit);
}
void Game_TraceShots(game_shot_traced traced)
{
    shot_listener = traced;
    P_ShotTraced = traced ? shot_traced : NULL;
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
