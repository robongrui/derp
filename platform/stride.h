/* The player's stride: where each player is between one footfall and the
 * next. The footsteps (audio/steps.c) sound when a foot comes down and the
 * weapon bob (A_WeaponReady) swings through the bottom of its arc at that
 * moment, both read from here, so what is heard and what is seen keep one
 * cadence. A step falls every stride walked on the ground, the stride
 * growing with speed: about two a second walking, three running. Landing
 * from a fall counts too. Compiled into each game against its own headers. */
#ifndef DOOM_STRIDE_H
#define DOOM_STRIDE_H
#ifdef __cplusplus
extern "C" {
#endif
struct mobj_s;

typedef struct {
    unsigned steps;      /* footfalls so far; each one bumps it */
    unsigned landings;   /* landings so far */
    float step_force;    /* the last footfall: 0 at a walk, 1 at a run */
    float landing_force; /* the last landing: 1, up to 2 for a hard fall */
    float phase;         /* 0 at a footfall, rising to 1 at the next */
    /* The swing in half-cycles, 0 up to 2: phase, plus 1 after an odd
       footfall, so a whole number at every one. The weapon sits low in
       the middle there and high at one side or the other halfway between.
       last is the tic before, for drawing between tics. */
    float swing, last_swing;
} stride_t;

/* Once a tic for each player, before the weapon is placed (P_PlayerThink),
   from where mo is now. shift is how far the world has carried things
   (Game_WorldShift: the library's), or NULL in a game without it. */
void Stride_Tic(int player, const struct mobj_s *mo, const float *shift);
const stride_t *Stride_Of(int player);
/* The swing drawn fraction of the way from the last tic to this one,
   in half-cycles, 0 up to 2. */
float Stride_Swing(int player, float fraction);
#ifdef __cplusplus
}
#endif
#endif
