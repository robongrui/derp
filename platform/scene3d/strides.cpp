// Legs that keep pace (DERP's monsters walking their steps out, p_strides.h).
// A walker's run frames follow the ground it covers, not the clock: at
// vanilla's pace that is vanilla's cadence; quicker, the legs go quicker
// and the stride grows a little (a run is longer strides, not only faster
// ones); held up, they stand. Drawing only: the game's states, and what
// the monster decides on them, run as ever.
#include "internal.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace doom3d {
namespace {
// A type's run: its chase states in order, the frames they show (each
// once, in order), the ground a frame stands for at vanilla's pace, and
// that pace (units a tic).
struct Cycle { std::vector<int> states,frames; float perFrame=0,pace=0; };
// A walker's: where it was last tic, how far through its run, its speed
// (units a tic, smoothed), and the sweep that last saw it.
struct Stride { float x,y,phase,speed; unsigned sweep; };
std::unordered_map<int,Cycle> cycles;
std::unordered_map<const mobj_t*,Stride> strides;
unsigned strideLevel=~0u,sweep;
}

// The run a type loops through once it is after something: from its
// seestate on to the loop it settles in (the arachnotron's sight frame
// leads into its run, not back to it); none if there is no loop, it shows
// a single frame, or another sprite takes part.
static const Cycle &cycleOf(int type) {
    auto found=cycles.find(type);
    if(found!=cycles.end())return found->second;
    Cycle c;
    const mobjinfo_t &info=mobjinfo[type];
    std::vector<int> path;
    int s=info.seestate,tics=0;
    while(s>0&&path.size()<32&&std::find(path.begin(),path.end(),s)==path.end()) {
        path.push_back(s);
        s=states[s].nextstate;
    }
    auto loop=std::find(path.begin(),path.end(),s);
    if(s>0&&loop!=path.end())c.states.assign(loop,path.end());
    for(int st:c.states) {
        tics+=std::max(0,(int)states[st].tics);
        int f=states[st].frame&FF_FRAMEMASK;
        if(states[st].sprite!=states[c.states[0]].sprite) {c.frames.clear();tics=0;break;}
        if(c.frames.empty()||c.frames.back()!=f)c.frames.push_back(f);
    }
    if(c.frames.size()>1&&c.frames.front()==c.frames.back())c.frames.pop_back();
    if(c.frames.size()<2||tics<=0||info.speed<=0) {
        c.frames.clear();
    } else {
        // Each chase state is one step of speed units (A_Chase's P_Move).
        c.perFrame=(float)c.states.size()*info.speed/c.frames.size();
        c.pace=(float)c.states.size()*info.speed/tics;
    }
    return cycles.emplace(type,std::move(c)).first->second;
}

// As the game's (p_strides.c): what walks, alive, while DERP's monsters play.
static bool walker(const mobj_t &thing) {
    return (thing.flags&MF_COUNTKILL)&&!(thing.flags&(MF_FLOAT|MF_NOGRAVITY))&&thing.health>0
        &&Game_StrideLegs(&thing);
}

void watchStrides() {
    if(strideLevel!=r_levelserial) {strides.clear();strideLevel=r_levelserial;}
    ++sweep;
    for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        if(!walker(*thing))continue;
        float x=units(thing->x),y=units(thing->y);
        auto found=strides.find(thing);
        if(found==strides.end()) {strides.emplace(thing,Stride{x,y,0,0,sweep});continue;}
        Stride &st=found->second;
        float d=std::hypot(x-st.x,y-st.y);
        if(d>64)d=0;	// a teleport, or another thing in a freed one's memory
        st.speed=st.speed*0.7f+d*0.3f;
        st.x=x;st.y=y;st.sweep=sweep;
        const Cycle &c=cycleOf(thing->type);
        if(c.frames.empty())continue;
        // Quicker than vanilla's pace, longer strides: by the root of it.
        float per=c.perFrame*std::sqrt(std::max(1.0f,c.pace>0?st.speed/c.pace:1.0f));
        st.phase=std::fmod(st.phase+d/per,(float)c.frames.size());
    }
    for(auto it=strides.begin();it!=strides.end();)
        it=it->second.sweep==sweep?std::next(it):strides.erase(it);
}

// (backwards: the same steps in the other order, a walk backwards.)
int strideLegs(const mobj_t &thing,bool backwards) {
    const Cycle &c=cycleOf(thing.type);
    if(c.frames.empty())return -1;
    auto found=strides.find(&thing);
    // Strides off: by the clock, at the run's own pace.
    size_t at=found!=strides.end()?(size_t)found->second.phase:(size_t)leveltime/4;
    at%=c.frames.size();
    return c.frames[backwards?c.frames.size()-1-at:at];
}

bool strideView(const mobj_t &thing,DeathView &view) {
    if(!walker(thing))return false;
    auto found=strides.find(&thing);
    if(found==strides.end())return false;
    const Cycle &c=cycleOf(thing.type);
    int at=(int)(thing.state-states);
    if(c.frames.empty()||std::find(c.states.begin(),c.states.end(),at)==c.states.end()
       ||states[at].sprite!=view.sprite)return false;
    int f=c.frames[(size_t)found->second.phase%c.frames.size()];
    view.frame=(view.frame&~FF_FRAMEMASK)|f;
    return true;
}

}
