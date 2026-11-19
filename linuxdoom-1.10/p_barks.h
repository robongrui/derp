// What DERP's soldiers shout: they have no radios. Each line goes with something
// the AI has just decided, so what you hear is what they are about to do.
//
// One voice at a time in earshot, the same line not twice running, and an answer now and
// then from a friend nearby ("Moving!" - "Covering!"). Zombiemen speak with one
// voice, shotgun guys with another, chaingunners with a third where they have the
// line and the shotgun guys' where not. The recordings are DMX lumps beside the game
// (scripts/make_barks.py, add_barks.py, add_voices.py): the cries without words in
// derp-cries.wad, shipped with it; the spoken lines in derp-voices.wad, a separate
// download, without which the soldiers only cry out.
//
// Imps do not speak, but they have a voice of their own all the same (P_DerpCry):
// wet throat sounds, a call and an answer, in place of everything id gave them.
//
// A bark is only heard by him. What the monsters tell each other is NOISE_SHOUT.
#ifndef __P_BARKS__
#define __P_BARKS__

#include "p_mobj.h"

// In scripts/make_barks.py's MOMENTS order: the numbers are in the lump names.
typedef enum
{
    BARK_CONTACT,
    BARK_NOISE,
    BARK_NOTHING,
    BARK_LOSTYOU,
    BARK_TAKINGFIRE,
    BARK_COVER,
    BARK_FLANKED,
    BARK_BOUNDING,
    BARK_COVERINGBOUND,
    BARK_SUPPRESSING,
    BARK_PINNED,
    BARK_FRIENDINLINE,
    BARK_FRIENDDIES,
    BARK_TRAP,
    BARK_DOOR,
    BARK_WATCHDOOR,
    BARK_RESERVE,
    BARK_KEY,
    BARK_FALLBACK,
    BARK_REGROUP,
    BARK_LASTALIVE,
    // Not said but let out: nobody takes turns over these (P_DerpBody).
    BARK_PAIN,
    BARK_IDLE,
    BARK_GIBBED,
    BARK_DEATH,
    // The later recordings (scripts/add_barks.py): the soldiers' new moves,
    // the chaingunner's own and his rare jokes, and the imps' whole voice.
    BARK_PUSH,
    BARK_PUSHHURT,
    BARK_PUSHAWAY,
    BARK_GOROUND,
    BARK_TOOCLOSE,
    BARK_BAIT,
    BARK_BAITTURN,
    BARK_FRIENDHIT,
    BARK_DEMONHIT,
    BARK_GETDEMON,
    BARK_PINKYCHARGE,
    BARK_PINKYPOUND,
    BARK_WINDOW,
    BARK_TOOKITEM,
    BARK_TRAPSPRUNG,
    BARK_RUNNING,
    BARK_HEADSHOT,
    BARK_FRIENDGIBBED,
    BARK_INSLIME,
    BARK_NOSLIME,
    BARK_FLASHLIGHT,
    BARK_BARRELS,
    BARK_FIRES,
    BARK_LANE,
    BARK_WAITING,
    BARK_STILLALIVE,
    BARK_SHRUG,
    BARK_JOKEMATH,
    BARK_JOKEFREE,
    BARK_JOKEPRESSURE,
    BARK_JOKECANTSEE,
    BARK_JOKEFEATURE,
    BARK_JOKEREALLIFE,
    BARK_JOKEWANTED,
    BARK_JOKELOVEGORE,
    BARK_JOKEUNNECESSARY,
    BARK_JOKESPLASH,
    BARK_JOKERIGHTKEYS,
    BARK_JOKEWHATISIT,
    BARK_JOKEDONTTALK,
    BARK_JOKETWICE,
    BARK_IMPSEES,
    BARK_IMPANSWER,
    BARK_IMPCLICKS,
    BARK_IMPWINDUP,
    BARK_IMPPOUNCE,
    BARK_IMPSWIPE,
    BARK_IMPTHROW,
    BARK_IMPPAINSMALL,
    BARK_IMPPAINBIG,
    BARK_IMPDEATH,
    BARK_IMPGIBBED,
    BARK_IMPHEARD,
    BARK_IMPSNIFF,
    BARK_IMPPACKDEATH,
    BARK_IMPCORNERED,
    BARK_IMPLAND,
    BARK_IMPWINDOW,
    BARK_IMPCHITTER,
    // y.A.n.I.k.a.d.'s lines (scripts/add_voices.py). Past 99 the lump name has a letter.
    BARK_FLANKDOOR,
    BARK_FLANKOTHER,
    BARK_HEARSRUN,
    BARK_BULLETNEAR,
    BARK_GOESLOOK,
    BARK_WORDROUND,
    BARK_WORDANSWER,
    BARK_STANDSREADY,
    BARK_STANDSDOWN,
    BARK_HIDDENDOOR,
    BARK_GUARDSITEM,
    BARK_FRIENDGONE,
    BARK_HOLDBACK,
    BARK_GOALL,
    BARK_IMPSQUIET,
    BARK_IMPOVER,
    BARK_CHATREMEMBER,
    BARK_CHATINSIDE,
    BARK_CHATBODY,
    BARK_CHATHIM,
    BARK_CHATJOB,
    BARK_CHATCLOWN,
    BARK_TALK01,
    BARK_TALK02,
    BARK_TALK03,
    BARK_TALK04,
    BARK_TALK05,
    BARK_TALK06,
    BARK_TALK07,
    BARK_TALK08,
    BARK_TALK09,
    BARK_TALK10,
    BARK_IMPLURK,
    BARK_IMPSCRAPE,
    BARK_IMPCALL,
    BARK_IMPCALLBACK,
    BARK_IMPCROUCH,
    BARK_IMPAIR,
    BARK_IMPOVERBODY,
    NUMBARKS
} bark_t;

// F4 Sound's Monster voices: off, nobody barks, cries or keeps quiet, and every
// monster makes id's own sounds; the AI decides as ever. P_DerpVoices: they are heard.
void P_SetMonsterVoices (boolean on);
boolean P_DerpVoices (void);
// A level starts: nobody speaking, and which recordings there are.
void P_BarksLevel (void);
// Each tic: an answer that is due.
void P_BarksThink (void);
// actor says it, if it can and nobody else is speaking. False: it did not.
boolean P_DerpBark (mobj_t* actor, bark_t moment);
// The same in a moment, if it is still alive then: for what a shot sets off,
// when the next pellet may be the end of it.
void P_DerpBarkSoon (mobj_t* actor, bark_t moment);
// Whether its idle growl stays unsaid: a soldier has its shouts, and the user
// found any mutter annoying sooner or later. (BARK_IDLE's recordings are unused.)
boolean P_DerpHushed (mobj_t* actor);
// A sound of its body in its own voice, at once, whoever else is speaking and
// alive or not. False: none, so it makes id's.
boolean P_DerpBody (mobj_t* actor, bark_t moment);
// The nearest soldier awake within range of (x, y) says it.
void P_DerpBarkNear (fixed_t x, fixed_t y, int range, bark_t moment);
// The same from one who sees victim, and is not victim.
void P_DerpBarkSawFall (mobj_t* victim, int range, bark_t moment);
// It died: what it was saying stops there.
void P_DerpCutOff (mobj_t* mo);
// Line take of that moment, as recorded, nobody else talking: for what is said in order
// (p_chatter.c). False if somebody is, or it has no such line in its voice.
boolean P_DerpSayTake (mobj_t* actor, bark_t moment, int take);
// How many lines it has for that moment; whether somebody is talking in its earshot.
int P_DerpTakes (const mobj_t* actor, bark_t moment);
boolean P_DerpSpeaking (const mobj_t* mo);
// An imp's cry. True: it is an imp and the sound is its own, so no stock one,
// even if that cry is too soon after the last to play.
boolean P_DerpCry (mobj_t* actor, bark_t moment);
// target is about to take damage from source: what a soldier makes of that, and
// the imps round about, if he is the one going down.
void P_DerpBarkHit (mobj_t* target, mobj_t* source, int damage);

#endif
