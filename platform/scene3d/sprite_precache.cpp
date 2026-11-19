/* The level's sprites made under the loading screen: every frame and rotation
   the things on the map can show, so a monster's first attack, pain or death
   does not decode, shade and upload its artwork in the middle of a fight. */
#include "internal.h"

namespace doom3d {
namespace {
// Marks the sprite lumps of every state from start on, following nextstate
// until the chain ends or meets a state already walked (a loop, or a chain
// another one led into). want may be null: then it only marks the states.
// stop: states the walk ends at without marking them.
void walkStates(int start,std::vector<char> &walked,std::vector<char> *want,const std::vector<char> *stop=nullptr) {
    for(int s=start,steps=0;s>0&&s<NUMSTATES&&!walked[s]&&!(stop&&(*stop)[s])&&steps<4096;s=(int)states[s].nextstate,++steps) {
        walked[s]=1;
        if(!want)continue;
        const state_t &state=states[s];
        int sprite=(int)state.sprite,frame=state.frame&FF_FRAMEMASK;
        if(sprite<0||sprite>=numsprites||frame>=sprites[sprite].numframes)continue;
        for(short rotation:sprites[sprite].spriteframes[frame].lump)
            if(rotation>=0&&firstspritelump+rotation<numlumps)(*want)[firstspritelump+rotation]=1;
    }
}
void walkType(const mobjinfo_t &info,std::vector<char> &walked,std::vector<char> *want) {
    // The chains both games' tables have. Doom's raise frames are its death
    // frames run backward, so they are loaded already.
    for(int start:{(int)info.spawnstate,(int)info.seestate,(int)info.painstate,(int)info.meleestate,
                   (int)info.missilestate,(int)info.deathstate,(int)info.xdeathstate})
        walkStates(start,walked,want);
}
} // namespace

// Which thing types to load: those on the map; what can appear without being
// placed (missiles, puffs, blood, fog: the types no map can hold); every
// pickup, so what a monster drops is ready; the monsters outside the kill
// count (the Lost Soul a Pain Elemental spits) where there are monsters at
// all. The state table's rest, the states no thing type leads to, are mostly
// the player's weapons: their frames, the flashes included. A few are
// monsters' frames reached only from code; they come along.
//
// Left to first sight: the images made from a frame (a sleeper's, tinted
// blood, a soldier's waist, wounds, dolls and gore pieces, the relit weapon),
// frames a DEHACKED patch reaches only through a raise state, and monsters
// that only a spawner or a script brings in.
void precacheSprites() {
    int paletteLump=W_CheckNumForName((char*)"PLAYPAL");
    if(paletteLump<0||W_LumpLength(paletteLump)<768||numlumps<=0)return;
    std::array<byte,768> palette;
    memcpy(palette.data(),W_CacheLumpNum(paletteLump,PU_CACHE),palette.size());
    std::vector<char> present(NUMMOBJTYPES,0);
    bool monsters=false;
    for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(thing->type>=0&&thing->type<NUMMOBJTYPES) {
            present[thing->type]=1;
            if(mobjinfo[thing->type].flags&MF_COUNTKILL)monsters=true;
        }
    std::vector<char> walked(NUMSTATES,0),want(numlumps,0);
    for(int type=0;type<NUMMOBJTYPES;++type) {
        const mobjinfo_t &info=mobjinfo[type];
        bool load=present[type]||info.doomednum==-1||(info.flags&(MF_MISSILE|MF_SPECIAL))||
            (monsters&&Game_ThingRole(type)==GAME_THING_UNCOUNTED_MONSTER);
        if(load)walkType(info,walked,&want);
    }
    // The states every thing type leads to, loaded or not, then the rest, each
    // up to where it joins a thing type's own chain (an Arch-vile's heal runs
    // back into his walk, which only an Arch-vile on the map needs).
    std::vector<char> reached(NUMSTATES,0);
    for(int type=0;type<NUMMOBJTYPES;++type)walkType(mobjinfo[type],reached,nullptr);
    for(int s=1;s<NUMSTATES;++s)if(!reached[s])walkStates(s,walked,&want,&reached);
    // The weapon frames drawn relit (weapon.cpp) are de-lit from the artwork
    // the first time each shows, a dozen milliseconds apiece: a first shot
    // hitched once per frame of the gun's fire and reload. So those too, once
    // their sprites are in.
    std::vector<int> relit;
    for(int s=1;s<NUMSTATES;++s) {
        const state_t &state=states[s];
        if(reached[s]||(state.frame&FF_FULLBRIGHT)||state.sprite<0||state.sprite>=numsprites)continue;
        const spritedef_t &def=sprites[state.sprite];
        int frame=state.frame&FF_FRAMEMASK;
        if(frame>=def.numframes||!def.spriteframes||def.spriteframes[frame].lump[0]<0)continue;
        relit.push_back(firstspritelump+def.spriteframes[frame].lump[0]);
    }
    std::sort(relit.begin(),relit.end());relit.erase(std::unique(relit.begin(),relit.end()),relit.end());
    auto relightWeapons=[&] {
        for(size_t n=0;n<relit.size();++n) {
            if(images.count(-1-relit[n]))precacheWeaponImage(relit[n]);
            if(n%4==0)loadingTick(1);
        }
    };
    // The player's weapon as it is now, whatever leads to it.
    for(int p=0;p<MAXPLAYERS;++p)if(playeringame[p])
        for(int i=0;i<NUMPSPRITES;++i)if(const state_t *state=players[p].psprites[i].state)walkStates((int)(state-states),walked,&want);
    // Copies of the lumps not loaded yet (the zone may drop a cached lump
    // while the next is read), with what each one gets.
    struct Job { int lump; std::vector<byte> data; char name[9]; bool eyes,flash,slime,ok=false; SpriteArt art; };
    std::vector<Job> jobs;
    for(int lump=0;lump<numlumps;++lump) {
        if(!want[lump]||images.count(-1-lump))continue;
        int size=W_LumpLength(lump);
        if(size<=0)continue;
        jobs.emplace_back();Job &job=jobs.back();
        const byte *data=(const byte*)W_CacheLumpNum(lump,PU_CACHE);
        job.lump=lump;job.data.assign(data,data+size);
        memcpy(job.name,lumpinfo[lump].name,8);job.name[8]=0;
        job.eyes=spriteEyes(lump);job.flash=spriteFlash(lump);job.slime=spriteSlime(lump);
    }
    if(jobs.empty()) {relightWeapons();return;}
    // Decoded and shaded on worker threads and this one, which keeps the
    // loading screen drawn between its own jobs.
    std::atomic<size_t> next{0},finished{0};
    // Takes the next job; false when none are left.
    auto runJob=[&] {
        size_t n=next++;
        if(n>=jobs.size())return false;
        Job &job=jobs[n];
        job.ok=!decodeSprite(job.data.data(),(int)job.data.size(),job.art);
        if(job.ok)finishSpriteArt(job.art,job.name,palette.data(),job.eyes,job.flash,job.slime);
        ++finished;
        return true;
    };
    std::vector<std::thread> workers;
    unsigned helpers=std::min(15u,std::max(1u,std::thread::hardware_concurrency())-1);
    try {for(unsigned t=0;t<helpers&&t+1<jobs.size();++t)workers.emplace_back([&]{while(runJob()){}});}
    catch(...) {}   // no more threads: fewer hands
    while(runJob())loadingTick(0.5f*(float)finished.load()/(float)jobs.size());
    for(std::thread &worker:workers)worker.join();
    // Uploaded here, in order. A lump that is no sprite patch is left to
    // first sight, where lumpImage reports it as before.
    for(size_t n=0;n<jobs.size();++n) {
        Job &job=jobs[n];
        if(job.ok&&!images.count(-1-job.lump))keepSprite(job.lump,job.art);
        job.data.clear();job.data.shrink_to_fit();
        if(n%16==0)loadingTick(0.5f+0.5f*n/jobs.size());
    }
    relightWeapons();
}
} // namespace doom3d
