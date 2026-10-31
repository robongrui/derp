/* Craters: nukage rain (nukage_rain.cpp) eating the monsters out in it.
   - Drops splash off their tops in nukage-coloured specks.
   - They soak: a translucent darker green creeping down the sprite from the
     top in mottled texels over about 25 seconds of downpour (bodySoak; the
     shader paints it through the frost channel, world.frag), drying slowly
     out of it.
   - Bites: each drop takes the texel it hits out of the top edge of
     whatever frame is drawn, the next one in that column deeper, so they
     slowly dissolve from the top down; a bite grows over a few seconds, a
     texel deeper, then into the column beside it. About two a second; the
     bites stay on the corpse like the wound holes.
   - A corpse is eaten the same way, faster, a bite landing where its column
     is eaten through going on to the next that isn't; once nothing is left
     it is no longer drawn at all (dissolvedAway; raised again, it comes back
     whole).
   - Smoke: steam off everything soaked, the thicker the more it is, and
     brighter wisps off each fresh bite.
   A cheap effect, nothing simulated: the bites are holes in the wound mask
   (wounds.cpp). Render-only, with its own random numbers. */
#include "internal.h"

namespace doom3d {
namespace {
// A bite: where across the drawing it lands, when, and whether a corpse
// took it (two texels to start, no smoke of its own).
struct Crater { float across; uint32_t seed; int born; bool corpse; };
std::unordered_map<const mobj_t*,std::vector<Crater>> craters;
// How soaked each thing is, 0-1.
std::unordered_map<const mobj_t*,float> soaked;
// Per drawing: each column's topmost and lowest opaque row (-1: none), and
// the span of columns that have one.
struct Tops { int first=-1,last=-1;std::vector<int> row,bottom; };
std::unordered_map<const Image*,Tops> tops;
// Corpses eaten through: nothing of them left to draw.
std::unordered_set<const mobj_t*> eaten;
// Each bitten monster's last drawing and its columns' highest texel left
// after the bites (craterMask), where the splashes land.
struct Bitten { const Image *image=nullptr;std::vector<int> top; };
std::unordered_map<const mobj_t*,Bitten> bittenTops;
constexpr size_t maxCraters=160;  // a big monster wears down, never wholly away
constexpr size_t maxCorpseBites=4000;
constexpr float bitesASecond=2,corpseBitesASecond=14;
constexpr float soakSeconds=25,drySeconds=90;
constexpr int growTics=3*TICRATE;  // a bite takes another texel this often, twice
constexpr int biteSmokeTics=4*TICRATE;
int lastTic=-1;
unsigned cratersLevel=~0u;

const Tops &topsOf(const Image &image) {
    auto found=tops.find(&image);
    if(found!=tops.end())return found->second;
    Tops &t=tops[&image];
    t.row.assign(image.width,-1);t.bottom.assign(image.width,-1);
    for(int c=0;c<image.width;++c)for(int r=0;r<image.height;++r)
        if(image.pixels[((size_t)r*image.width+c)*2+1]) {
            if(t.row[c]<0)t.row[c]=r;
            t.bottom[c]=r;if(t.first<0)t.first=c;t.last=c;
        }
    return t;
}
bool monster(const mobj_t &thing) {
    return thing.info&&((thing.info->flags&MF_COUNTKILL)||Game_ThingRole(thing.type)==GAME_THING_UNCOUNTED_MONSTER);
}
bool exposedCorpse(const mobj_t &thing) {
    return monster(thing)&&thing.health<=0&&(thing.flags&MF_CORPSE)&&weatherReaches(thing);
}
bool exposedMonster(const mobj_t &thing) {
    return monster(thing)&&thing.health>0&&!(thing.flags&MF_CORPSE)&&weatherReaches(thing);
}
// A new level: none of the last one's craters or drawings carry over (their
// addresses may come back as new things and images).
void fresh() {
    if(cratersLevel==r_levelserial)return;
    cratersLevel=r_levelserial;craters.clear();soaked.clear();eaten.clear();tops.clear();bittenTops.clear();lastTic=-1;
}
// The top of thing as drawn, roughly: a standing monster's head, a corpse's back.
float topOf(const mobj_t &thing) {return units(thing.z)+(thing.health>0?units(thing.height)*0.95f:6);}
}

// Each frame while nukage rain falls: on every new tic each monster out in
// it soaks a little more and may take a bite, those out of it dry a little;
// then the splashes and smoke near the eye.
void watchCraters(const Uniforms &camera) {
    fresh();
    if(lastTic<0||leveltime<lastTic||leveltime-lastTic>TICRATE)lastTic=leveltime-1;
    for(int tic=lastTic+1;tic<=leveltime;++tic)
        for(int s=0;s<numsectors;++s)
            for(mobj_t *thing=sectors[s].thinglist;thing;thing=thing->snext) {
                if(!monster(*thing))continue;
                bool corpse=exposedCorpse(*thing),out=corpse||exposedMonster(*thing);
                if(!out) {
                    auto wet=soaked.find(thing);
                    if(wet!=soaked.end())wet->second=std::max(0.0f,wet->second-1.0f/(drySeconds*TICRATE));
                    continue;
                }
                float &soak=soaked[thing];
                soak=std::min(1.0f,soak+fallNow/(soakSeconds*TICRATE));
                if(corpse&&eaten.count(thing))continue;
                uint32_t id=hash32((uint32_t)(uintptr_t)thing^0x2c1b3c6du);
                if(hashUnit(tic,0,id)>=fallNow*(corpse?corpseBitesASecond:bitesASecond)/TICRATE)continue;
                auto &list=craters[thing];
                if(list.size()>=(corpse?maxCorpseBites:maxCraters))continue;
                uint32_t seed=hash32(id^(uint32_t)tic*2654435761u);
                list.push_back({hashUnit(1,0,seed),seed,tic,corpse});
            }
    lastTic=leveltime;
    float now=gameSeconds();
    auto nukage=nukageColor();
    std::array<float,3> smoke,splash;
    for(int c=0;c<3;++c) {smoke[c]=0.36f+0.3f*nukage[c];splash[c]=0.25f+0.75f*nukage[c];}
    float right[2]={camera.right[0],camera.right[1]},flat=std::hypot(right[0],right[1]);
    if(flat>1e-3f) {right[0]/=flat;right[1]/=flat;}
    for(const auto &[thing,soak]:soaked) {
        if(!thing->subsector||eaten.count(thing)||soak<=0)continue;
        float x=units(thing->x),y=units(thing->y),distance=std::hypot(x-camera.eye[0],y-camera.eye[1]);
        if(distance>512)continue;
        const sector_t &sector=*thing->subsector->sector;
        bool out=thing->health>0?exposedMonster(*thing):exposedCorpse(*thing);
        float top=topOf(*thing),wide=units(thing->radius)*(thing->health>0?1.6f:2.0f);
        uint32_t id=hash32((uint32_t)(uintptr_t)thing^0x7feb352du);
        // Splashes on the top while it rains on it: four drops landing in
        // turn, each on the drawn outline (where the wound holes saw the
        // sprite drawn, woundSpriteView): the top texel of a column, two specks
        // thrown up and aside from it over a few tics.
        SpriteView view;
        if(out&&distance<384&&thing->health>0&&woundSpriteView(*thing,view)) {
            const Tops &edge=topsOf(*view.image);
            int w=view.image->width,h=view.image->height;
            float left=-(float)view.image->left,rowHeight=(view.top-view.bottom)/std::max(h,1);
            for(int n=0;n<4&&edge.first>=0;++n) {
                float period=0.3f+0.25f*hashUnit(n,5,id),clock=now+hashUnit(n,6,id)*period;
                int cycle=(int)std::floor(clock/period);
                float t=(clock-cycle*period)/0.12f;
                if(t>=1)continue;
                uint32_t drop=hash32(id^(uint32_t)cycle*2654435761u^(uint32_t)n);
                int column=edge.first+(int)(hashUnit(n,7,drop)*(edge.last-edge.first+1));
                if(column>edge.last||edge.row[column]<0)continue;
                // On what the bites left of that column.
                int row=edge.row[column];
                auto bit=bittenTops.find(thing);
                if(bit!=bittenTops.end()&&bit->second.image==view.image&&column<(int)bit->second.top.size())row=bit->second.top[column];
                if(row>edge.bottom[column])continue;   // eaten through
                float along=view.flipped?left+w-(column+0.5f):left+column+0.5f,aside=0.6f+1.5f*t;
                float ex=view.x+view.rx*along,ey=view.y+view.ry*along,ez=view.top-row*rowHeight;
                for(int side=-1;side<=1;side+=2) {
                    float px=ex+view.rx*side*aside,py=ey+view.ry*side*aside,pz=ez+0.5f+6*t*(1-t);
                    pixelBlock(particleVertices,camera,px,py,pz,0.5f,1,weatherColor(sector,px,py,pz,splash),false,nullptr,true,nullptr,0.8f*(1-t));
                }
            }
        }
        // Steam off it, thicker the more soaked: up to ten wisps rising some
        // sixteen units and drifting with the wind; while it is out in it.
        if(out) {
            int wisps=2+(int)std::lround(8*soak);
            for(int n=0;n<wisps;++n) {
                float period=1.4f+0.6f*hashUnit(n,1,id),t=std::fmod(now+hashUnit(n,2,id)*period,period)/period;
                float across=(hashUnit(n,3,id)-0.5f)*wide;
                float px=x+right[0]*across+windNow[0]*0.006f*t,py=y+right[1]*across+windNow[1]*0.006f*t,pz=top+1+16*t;
                pixelBlock(particleVertices,camera,px,py,pz,t<0.5f?0.7f:0.5f,1,weatherColor(sector,px,py,pz,smoke),false,nullptr,true,nullptr,(0.35f+0.4f*soak)*(1-t));
            }
        }
        // And a brighter puff off each fresh bite.
        auto bitten=craters.find(thing);
        if(bitten==craters.end())continue;
        for(const Crater &crater:bitten->second) {
            int age=leveltime-crater.born;
            if(crater.corpse||age<0||age>biteSmokeTics)continue;
            float fade=1-age/(float)biteSmokeTics;
            for(int n=0;n<3;++n) {
                float period=1.0f+0.4f*hashUnit(n,1,crater.seed),t=std::fmod(now+hashUnit(n,2,crater.seed)*period,period)/period;
                float px=x+right[0]*(crater.across-0.5f)*wide+windNow[0]*0.006f*t;
                float py=y+right[1]*(crater.across-0.5f)*wide+windNow[1]*0.006f*t;
                float pz=top+1+14*t;
                pixelBlock(particleVertices,camera,px,py,pz,0.7f,1,weatherColor(sector,px,py,pz,smoke),false,nullptr,true,nullptr,0.7f*(1-t)*fade);
            }
        }
    }
}

// The bites in a drawing of thing, as holes (1) in the wound mask: each the
// highest texel of its column still there, growing as it ages; sized: the
// mask is already the drawing's size (shot holes in it). Whether any show.
bool craterMask(const mobj_t &thing,const Image &image,std::vector<byte> &mask,bool sized) {
    fresh();
    auto found=craters.find(&thing);
    if(found==craters.end()||found->second.empty()||image.pixels.empty())return false;
    const Tops &t=topsOf(image);
    if(t.first<0)return false;
    int w=image.width,h=image.height;
    if(!sized)mask.assign((size_t)w*h,0);
    bool any=false;
    // Each column's highest texel still there (past its bottom: eaten
    // through), moving down as the bites take them.
    std::vector<int> next(t.row);
    auto remaining=[&](int c) {
        if(c<0||c>=w)return false;
        int &r=next[c];
        if(r<0)return false;
        while(r<=t.bottom[c]&&(!image.pixels[((size_t)r*w+c)*2+1]||mask[(size_t)r*w+c]==1))++r;
        return r<=t.bottom[c];
    };
    auto take=[&](int c) {if(remaining(c)) {mask[(size_t)next[c]*w+c]=1;any=true;}};
    for(const Crater &crater:found->second) {
        int column=t.first+(int)std::lround(crater.across*(t.last-t.first));
        // A corpse's bite goes on to the next column with something left.
        for(int step=0;step<w&&!remaining(column);++step) {
            if(!crater.corpse&&t.row[column]>=0)break;
            column=(column+1)%w;
        }
        // Grown: a texel deeper after growTics, then the column beside it.
        int grown=std::min(2,std::max(0,leveltime-crater.born)/growTics),side=(crater.seed&1)?1:-1;
        take(column);
        if(crater.corpse)take(column);
        if(grown>=1)take(column);
        if(grown>=2)take(column+side);
    }
    (void)h;
    if(thing.health>0) {
        Bitten &left=bittenTops[&thing];
        left.image=&image;left.top.resize(w);
        for(int c=0;c<w;++c) {remaining(c);left.top[c]=next[c];}
    }
    // A corpse with nothing left: eaten.
    if(thing.health<=0) {
        bool left=false;
        for(int c=t.first;c<=t.last&&!left;++c)left=remaining(c);
        if(!left)eaten.insert(&thing);
    }
    return any;
}
unsigned bodySoak(const mobj_t &thing) {
    if(soaked.empty())return 0;
    fresh();
    auto found=soaked.find(&thing);
    return found==soaked.end()?0u:(unsigned)std::lround(found->second*255);
}
bool dissolvedAway(const mobj_t &thing) {
    if(eaten.empty())return false;
    fresh();
    return eaten.count(&thing)!=0;
}
void forgetCraters(const mobj_t *thing) {craters.erase(thing);soaked.erase(thing);eaten.erase(thing);bittenTops.erase(thing);}
void cratersShutdown() {craters.clear();soaked.clear();eaten.clear();tops.clear();bittenTops.clear();lastTic=-1;cratersLevel=~0u;}
} // namespace doom3d
