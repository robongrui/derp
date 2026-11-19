/* Gore (F4 Game, Gore): killing blows tear bodies apart. The rules: what a
   killing blow does to the head (stays on, blown apart, severed, torn), cuts
   in half, whole bodies bursting; the stump's beat; and the sounds gore makes
   (audio/gore_synth.c). A rule of the game like nukage rain, off in demos and
   netgames (PLAN-gore.md). */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
bool goreOn() {return settings.gore&&!demoplayback&&!netgame;}

// How much a part of this monster takes to come off: its health, but 20 to 50. Heads and limbs are about
// the same size on all of them, and at its full health an imp's (60) took three times a zombieman's: heads
// blown apart and arms off were rare, a head flying off the easy one (the user, 2026-11-03).
static int toughness(const mobj_t &thing) {return std::clamp(thing.info->spawnhealth,20,50);}
// A kill past the game's gib line by hitscan (Doom would play its gib frames): gore takes it apart
// instead, the rules below lenient while it is decided (the user: "super shotgun is an instagib, I
// never see limbs going missing or even the head").
static bool gibKill=false;
void goreGibKill(bool on) {gibKill=on;}
bool goreGibKilling() {return gibKill;}
// A kill by an explosion being decided: its damage this tic (0: none), for the limbs it tears off.
static int blastKill=0;
void goreBlastKill(int damage) {blastKill=damage;}
// How many limbs a blast that killed tears off: one at 1.5 times the monster's toughness, two at 2.5 (a
// rocket going off within a metre or so of an imp; a zombieman further out). -goreblast: two every time.
int goreBlastLimbs(const mobj_t &thing) {
    if(!blastKill||!thing.info)return 0;
    static int forced=-1;
    if(forced<0)forced=M_CheckParm((char*)"-goreblast")!=0;
    if(forced)return 2;
    int tough=toughness(thing);
    return blastKill>=tough*5/2?2:blastKill>=tough*3/2?1:0;
}

// The head (the user's three outcomes), by the monster's toughness (above): blown apart by two pellets or
// more on the head itself with its toughness in them, at least 30 (a zombieman: three shotgun pellets in the
// face; an imp: five); severed by four or more through the neck, no fewer than hit the head, with 6/5 of
// its toughness (at least 30); a chunk torn out at half of it on the head (at least 15). A gib kill: two pellets
// on the head blow it apart, three at the neck sever it, one tears it. -gorehead blown|severed|torn: every
// kill that hits the head or neck at all (test runs: the arena sets a victim's health so the last
// pellet only just kills it).
GoreHead goreHead(const mobj_t &thing,int headDamage,int headPellets,int neckDamage,int neckPellets) {
    if(!thing.info)return GORE_HEAD_STAYS;
    static int forced=-1;
    if(forced<0) {
        int arg=M_CheckParm((char*)"-gorehead");
        const char *which=arg&&arg+1<myargc?myargv[arg+1]:"";
        forced=!arg?GORE_HEAD_STAYS:!strcmp(which,"severed")?GORE_HEAD_SEVERED:!strcmp(which,"torn")?GORE_HEAD_TORN:GORE_HEAD_BLOWN;
    }
    if(forced!=GORE_HEAD_STAYS)return headPellets+neckPellets>0?(GoreHead)forced:GORE_HEAD_STAYS;
    int health=toughness(thing);
    if(gibKill) {
        if(headPellets>=2)return GORE_HEAD_BLOWN;
        if(neckPellets>=3&&neckPellets>=headPellets)return GORE_HEAD_SEVERED;
        return headPellets?GORE_HEAD_TORN:GORE_HEAD_STAYS;
    }
    if(headPellets>=2&&headDamage>=std::max(health,30))return GORE_HEAD_BLOWN;
    // Flying off whole: a row of pellets through the neck itself, more there than in the head. (Two with the
    // health in them made it the commonest head: a shotgun's pellets fly level, and on an imp the neck's band
    // is at the shoulders' height, so a shot at the top of the chest put the whole row in it; the user.)
    if(neckPellets>=4&&neckPellets>=headPellets&&neckDamage>=std::max(health*6/5,30))return GORE_HEAD_SEVERED;
    if(headPellets>=1&&headDamage>=std::max(health/2,15))return GORE_HEAD_TORN;
    return GORE_HEAD_STAYS;
}
// A big monster (the user: a hefty cut on death, the long fight's pay-off): as a part's toughness, the waist asks no
// more than a hundred of health. Asked for a Baron's thousand, no gun ever cut one.
// (The bosses die exploding: their frames are no legs to leave standing.)
bool goreHefty(const mobj_t &thing) {return thing.info&&thing.info->spawnhealth>=300&&thing.info->spawnhealth<2000;}
static int waistHealth(const mobj_t &thing) {return goreHefty(thing)?100:thing.info->spawnhealth;}
// Cut in half: four pellets or more across the waist with twice the monster's health in them, at least 40
// (a shotgun up close across a zombieman's middle); a gib kill, three. DERP is played with free aim, the
// pellets where the player puts them. With the game's autoaim (mouse look off, or Classic aim) every
// shot goes to the body's middle, the waist, and that took half of all shotgun kills on zombiemen: there
// it stays the edge case, six pellets with four times the health (at least 70), a gib kill five. -gorecut halves (or halveslow, the
// legs flying): every kill with a pellet there.
bool goreHalves(const mobj_t &thing,int waistDamage,int waistPellets) {
    if(!thing.info||!waistPellets)return false;
    static int forced=-1;
    if(forced<0) {int arg=M_CheckParm((char*)"-gorecut");forced=arg&&arg+1<myargc&&!strncmp(myargv[arg+1],"halves",6);}
    if(forced)return true;
    bool aimed=freeaim&&!classicaim;
    if(gibKill)return waistPellets>=(aimed?3:5);
    return aimed?waistPellets>=4&&waistDamage>=std::max(2*waistHealth(thing),40)
                :waistPellets>=6&&waistDamage>=std::max(4*waistHealth(thing),70);
}
// An arm off at the shoulder: two pellets or more on it with 3/5 of the monster's toughness in them, at least
// 20 (two or three pellets: a zombieman's arm, an imp's); a gib kill, any pellet on it. -gorearm: any pellet on an arm takes it off.
bool goreArm(const mobj_t &thing,int damage,int pellets) {
    if(!thing.info||!pellets)return false;
    static int forced=-1;
    if(forced<0)forced=M_CheckParm((char*)"-gorearm")!=0;
    if(forced)return true;
    if(gibKill)return true;
    return pellets>=2&&damage>=std::max(toughness(thing)*3/5,20);
}
// A leg off, at the knee or the hip (gore_legs.cpp), as an arm: two pellets on it with 3/5 of its toughness.
bool goreLeg(const mobj_t &thing,int damage,int pellets) {
    if(!thing.info||!pellets)return false;
    static int forced=-1;
    if(forced<0)forced=M_CheckParm((char*)"-goreleg")!=0;
    if(forced)return true;
    if(gibKill)return pellets>=2;
    return pellets>=2&&damage>=std::max(toughness(thing)*3/5,20);
}
// The whole body bursting (PLAN-gore.md G, H; body_burst.cpp), from the killing tic's damage. A telefrag
// always; a rocket's direct hit of twice the monster's health (a zombieman every time, an imp now and
// then); a monster with no rig (the joint finder turned the caco, pain elemental, mancubus,
// arachnotron, mastermind and Keen down) on half its health and at least 80 in one tic (a super shotgun
// point blank into a caco now and then, a BFG ball mostly). A ball of flesh on a rocket's direct hit whatever
// its damage (the user: a soft body otherwise, body_ball.cpp, because it's funny), and every time without
// physics deaths or with -goreballgib. -goreburst: every kill.
bool softBalls() {static int off=-1;if(off<0)off=M_CheckParm((char*)"-goreballgib")!=0;return !off&&settings.deathRagdoll;}
// Trial (2026-11-08): the Mancubus as a soft body too (-gorefatball). A skeleton bent his fat like a cookie; a sack
// of fat on a shell slaps down, wobbles and settles. Firmer than the Pain Elemental, his legs and cannons riding it.
constexpr int fatBallStiff=170;
bool fatBalls() {static int on=-1;if(on<0)on=M_CheckParm((char*)"-gorefatball")!=0;return on&&softBalls();}
// Trial (2026-11-08): the Mancubus dies on his own death frames (-gorefatframes), as Varied deaths play them, with a
// heavy landing: no doll, no soft body, no bending. Rebuilt from his pain drawing in 3D, both looked wrong (the user).
bool fatFrames() {static int on=-1;if(on<0)on=M_CheckParm((char*)"-gorefatframes")!=0;return on;}
int ballStiffness(int type) {
    if(int stiff=Game_BodyBall(type))return stiff;
    return fatBalls()&&!fatFrames()&&Game_BodyFat(type)?fatBallStiff:0;
}
bool goreBurst(const mobj_t &thing,int damage,bool rocketDirect,bool rigged) {
    if(!thing.info)return false;
    static int forced=-1;
    if(forced<0)forced=M_CheckParm((char*)"-goreburst")!=0;
    if(forced)return true;
    if(Game_BodyBall(thing.type)&&(!softBalls()||rocketDirect))return true;
    int health=thing.info->spawnhealth;
    if(damage>=10000)return true;
    if(rocketDirect&&damage>=2*health)return true;
    return !rigged&&damage>=std::max(health/2,80);
}

// The stump's pump, tics after the cut: the strength of the beat on that tic (1 the first, 0 between
// beats or once it has run dry). Hollywood's heart: racing at first, a beat every 7 tics, slowing
// and weakening as the blood runs out over five or six seconds; each body's beats jittered by its seed.
float stumpPulse(int tics,uint32_t seed) {
    constexpr float fade=90,dry=0.08f;
    float at=0;
    for(int beat=0;beat<64;++beat) {
        float strength=std::exp(-at/fade);
        if(strength<dry)return 0;
        int t=(int)std::lround(at);
        if(t==tics)return strength;
        if(t>tics)return 0;
        at+=7+0.5f*beat+3*hashUnit(beat,0,seed);
    }
    return 0;
}

// Right after the cut an artery doesn't beat yet, it streams: near steady, thinning out over the first
// 28 tics, the beats riding on it (the user: a fountain that paints the walls while the body flies).
float stumpStream(int tics,uint32_t seed) {
    constexpr int lasts=28;
    if(tics<0||tics>=lasts)return 0;
    float left=1-tics/(float)lasts;
    return left*left*(0.75f+0.25f*hashUnit(tics,2,seed));
}

// The sounds: flesh tearing, bone snapping, a body blown apart, a chunk landing (small) or slapping down (big).
void goreSound(GoreSound sound,float x,float y,float z) {
    static const int kinds[]={IMPACT_GORE_TEAR,IMPACT_GORE_CRACK,IMPACT_GORE_BLOWUP,IMPACT_GORE_THUD,IMPACT_GORE_SLAP};
    I_MadeSound(kinds[sound],x,y,z);
}
// Blood pooled or pieces lying at (x,y), with the rule on (audio/gore_floor.h: a step there squelches).
int goreUnderfoot(float x,float y) {return goreOn()&&(bloodPoolUnder(x,y)||flyingPieceUnder(x,y));}
}
