/* Exercise an actual level and complete native save/load without desktop input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "g_game.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_state.h"
#include "p_local.h"
#ifdef DOOM_RENDER3D
#include "../platform/i_render3d.h"
#endif
extern void A_PosAttack(mobj_t *actor);
extern void P_BulletSlope(mobj_t *actor);
extern void P_GunShot(mobj_t *actor, boolean accurate);
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static mobj_t *aim_hit;
static fixed_t aim_startz, aim_endz;
static void observe_aim(mobj_t *shooter, mobj_t *hit,
                        fixed_t x1, fixed_t y1, fixed_t z1,
                        fixed_t x2, fixed_t y2, fixed_t z2)
{
    (void)shooter; (void)x1; (void)y1; (void)x2; (void)y2;
    aim_hit=hit; aim_startz=z1; aim_endz=z2;
}
static void check_freeaim(void)
{
    mobj_t *player=players[0].mo;
    int angle=player->angle>>ANGLETOFINESHIFT;
    mobj_t *target=P_SpawnMobj(player->x+48*finecosine[angle],
                             player->y+48*finesine[angle],ONFLOORZ,MT_POSSESSED);
    boolean previous_freeaim=freeaim;
    float previous_pitch=tourpitch;
    void (*previous_trace)(mobj_t *,mobj_t *,fixed_t,fixed_t,fixed_t,fixed_t,fixed_t,fixed_t)=P_ShotTraced;
    fixed_t upper;
    target->health=1000;
    P_AimLineAttack(player,player->angle,MISSILERANGE);
    if(linetarget!=target) I_Error("Smoke test: freeaim fixture has no visible target");
    freeaim=true;
    P_ShotTraced=observe_aim;
    tourpitch=0.2f;
    P_BulletSlope(player);
    P_GunShot(player,true);
    if(aim_hit!=target || aim_startz!=players[0].viewz)
        I_Error("Smoke test: aimed shot did not start at the eye and hit its target");
    upper=aim_endz;
    tourpitch=-0.2f;
    P_BulletSlope(player);
    P_GunShot(player,true);
    if(aim_hit!=target || upper-aim_endz<8*FRACUNIT)
        I_Error("Smoke test: freeaim snapped shots to the target's center");
    /* Classic aim: both pitches autoaim to the same spot on the target. */
    classicaim=true;
    tourpitch=0.2f;
    P_BulletSlope(player);
    P_GunShot(player,true);
    upper=aim_endz;
    tourpitch=-0.2f;
    P_BulletSlope(player);
    P_GunShot(player,true);
    if(aim_hit!=target || upper!=aim_endz)
        I_Error("Smoke test: classic aim did not autoaim at the target");
    classicaim=false;
    P_ShotTraced=previous_trace;
    freeaim=previous_freeaim; tourpitch=previous_pitch;
    P_RemoveMobj(target);
}

static int stage, saved_ammo, saved_health, player_flashes, enemy_flashes;
static void (*renderer_flash)(mobj_t *,int,int);
static void observe_flash(mobj_t *source,int weapon,int projectile)
{
    if(source->player) ++player_flashes; else ++enemy_flashes;
#ifdef DOOM_RENDER3D
    if(getenv("DOOM_CAPTURE_FLASH_TEST") &&
       ((source->player && player_flashes==1)||(!source->player && enemy_flashes==1))) I_Render3DScreenshot();
#endif
    if(renderer_flash) renderer_flash(source,weapon,projectile);
}
#ifdef DOOM_RENDER3D
static unsigned serial_before_change;
static int projectile_capture;
#endif
static void key(evtype_t type, int value)
{
    event_t event = {type, value, 0, 0};
    D_PostEvent(&event);
}
void I_TestFrame(void)
{
    if (gamestate != GS_LEVEL) return;
    if (stage == 0 && gametic >= 5)
    {
        check_freeaim();
#ifdef DOOM_RENDER3D
        if (!R_AcceleratedView || !R_MuzzleFlash) I_Error("Metal smoke test: GPU renderer or flash hook is inactive");
        I_Render3DToggleFlashlight(); /* Exercise beam/bounce during movement and firing. */
        I_Render3DLook(-80);
#endif
        renderer_flash=R_MuzzleFlash;R_MuzzleFlash=observe_flash;
        key(ev_keydown, 'w'); stage = 1;
    }
    else if (stage == 1 && gametic >= 20)
    {
#ifdef DOOM_RENDER3D
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
#endif
        key(ev_keyup, 'w'); key(ev_keydown, KEY_RCTRL); stage = 2;
    }
    else if (stage == 2 && gametic >= 35)
    {
        mobj_t *player=players[0].mo;
        if (abs(player->momx) > FRACUNIT/8 || abs(player->momy) > FRACUNIT/8)
            I_Error("Smoke test: player still drifts after releasing movement");
        mobj_t *shooter=P_SpawnMobj(player->x,player->y,ONFLOORZ,MT_POSSESSED);
        key(ev_keyup, KEY_RCTRL);
        shooter->target=player;
        A_PosAttack(shooter);
        P_RemoveMobj(shooter);
        if(enemy_flashes!=1) I_Error("Smoke test: enemy shot did not notify flash renderer");
#ifdef DOOM_RENDER3D
        /* Keep the projectile capture clear of the fixture shot's red pain tint. */
        players[0].damagecount=0;
        P_SpawnPlayerMissile(player,MT_BRUISERSHOT);
#endif
        stage = 3;
    }
#ifdef DOOM_RENDER3D
    else if (stage == 3 && gametic >= 45 && !projectile_capture)
    {
        if(getenv("DOOM_CAPTURE_FLASH_TEST")) I_Render3DScreenshot();
        projectile_capture=1;
    }
#endif
    else if (stage == 3 && gametic >= 60)
    {
#ifdef DOOM_RENDER3D
        I_Render3DToggleFlashlight(); /* Exercise the reflected-color fade-out. */
        I_Render3DLook(80);
#endif
        saved_ammo = players[0].ammo[am_clip];
        saved_health = players[0].health;
        if (player_flashes == 0) I_Error("Smoke test: player shots did not notify flash renderer");
        if (saved_ammo >= 50) I_Error("Smoke test: firing did not consume ammo");
        G_SaveGame(5, "Native smoke test"); stage = 4;
    }
    else if (stage == 4 && gametic >= 70 && gameaction == ga_nothing)
    {
        FILE *file = fopen("doomsav5.dsg", "rb");
        if (!file) I_Error("Smoke test: save file was not created");
        fclose(file);
        players[0].ammo[am_clip] = 1;
#ifdef DOOM_RENDER3D
        I_Render3DToggleFlashlight(); /* Keep it on through the load's level reset. */
        serial_before_change = r_levelserial;
#endif
        G_LoadGame("doomsav5.dsg"); stage = 5;
    }
    else if (stage == 5 && gametic >= 85 && gameaction == ga_nothing)
    {
        if (players[0].ammo[am_clip] != saved_ammo || players[0].health != saved_health)
            I_Error("Smoke test: saved player state was not restored");
        if (!players[0].mo) I_Error("Smoke test: player object was not restored");
#ifdef DOOM_RENDER3D
        /* Loading rebuilds the level, so this also covers the renderer's level reset. */
        if (r_levelserial <= serial_before_change)
            I_Error("Metal smoke test: loading did not rebuild the level");
#endif
        puts("Native gameplay, firing, save, and load passed.");
        I_Quit();
    }
}
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    FILE *config = fopen("smoke.cfg", "w");
    if (!config) return 1;
    fputs("key_up 119\nkey_fire 157\nsnd_channels 16\n", config);
    fclose(config);
    char *args[] = {"smoke", "-iwad", argv[1], "-warp", "1", "1",
        "-config", "smoke.cfg", "-nomonsters", "-nomusic"};
    myargc = sizeof(args)/sizeof(args[0]); myargv = args;
    D_DoomMain();
    return 0;
}
