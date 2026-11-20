/* Lighting baked at level load on the 2.5D map: rays walk from sector to
 * sector through linedef crossings, like doom_flash_at, for sun visibility,
 * static point and pool lights and bounce light. Data-free so tests/baked_lighting.cpp
 * can check it without a WAD. */
#ifndef DOOM_BAKED_LIGHTING_H
#define DOOM_BAKED_LIGHTING_H
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "flash_lighting.h"

// A box of a sector's lines (buildBakeTrees): count 0 has children at first
// and first+1, else it holds count entries of the sector's tree order from first.
struct BakeNode { float low[2],high[2]; int first,count; };
// things: decorations standing in (or reaching into) the sector; see BakeThing.
// tree, order: the lines in boxes (positions in lines), for sectors with many.
struct BakeSector { float floor,ceiling; bool sky; std::vector<int> lines; std::vector<int> things={};
                    std::vector<BakeNode> tree={}; std::vector<int> order={}; };
// A middle texture on a two-sided line, which blocks rays where it is opaque:
// palette index/coverage pairs, row-major. It repeats along the line (texture
// u = u + direction*distance from a) and spans bottom..top once, with row
// anchor - height.
struct BakeMask { const unsigned char *pixels=nullptr; int width=0,height=0; float u=0,direction=1,anchor=0,bottom=0,top=0; };
struct BakeLine { float ax,ay,bx,by; int front,back; BakeMask mask={}; }; // -1: no sector on that side.
// A decoration's sprite as an occluder: a billboard standing at (x,y) that
// turns to face each ray, so its shadow is the outline seen from the light.
// One texel per unit, columns from left (relative to x), rows down from top;
// pixels are palette index/coverage pairs. albedo: its drawing's average color, what it gives back of the
// light on it to the bounce; sector: where it stands.
struct BakeThing { float x,y,top,left; int width,height; const unsigned char *pixels; float albedo[3]={0.5f,0.5f,0.5f}; int sector=-1; };
struct BakeMap { std::vector<BakeSector> sectors; std::vector<BakeLine> lines; std::vector<BakeThing> things={}; };
// What stopped a ray. Walls belong to the side facing sector, the one the ray
// came from; part is 0 for middle (one-sided or masked), 1 upper, 2 lower.
// A thing hit has no line: line is the decoration's index in BakeMap::things.
struct BakeHit { enum Kind { none, floor, ceiling, wall, thing } kind=none; int sector=-1,line=-1,part=0; float x=0,y=0,z=0; };

// True where the line's middle texture covers the point at fraction u along it.
inline bool maskBlocks(const BakeLine &line,float u,float height) {
    const BakeMask &m=line.mask;
    if(!m.pixels||m.width<1||m.height<1||height<m.bottom||height>=m.top)return false;
    float distance=u*std::hypot(line.bx-line.ax,line.by-line.ay);
    int x=(int)std::floor(m.u+m.direction*distance)%m.width;if(x<0)x+=m.width;
    int y=std::clamp((int)std::floor(m.anchor-height),0,m.height-1);
    return m.pixels[2*((size_t)y*m.width+x)+1]!=0;
}

// Where a ray (from origin, along direction, between from and to) passes the
// billboard's plane, in ray length; true where the sprite covers that point.
inline bool thingBlocks(const BakeThing &thing,float x,float y,float z,const float direction[3],float from,float to,float &at) {
    float h2=direction[0]*direction[0]+direction[1]*direction[1];
    if(h2<1e-6f)return false;
    float t=((thing.x-x)*direction[0]+(thing.y-y)*direction[1])/h2;
    if(t<=from+1e-3f||t>=to)return false;
    float px=x+direction[0]*t,py=y+direction[1]*t,pz=z+direction[2]*t;
    float lateral=((px-thing.x)*direction[1]-(py-thing.y)*direction[0])/std::sqrt(h2);
    int column=(int)std::floor(lateral-thing.left),row=(int)std::floor(thing.top-pz);
    if(column<0||row<0||column>=thing.width||row>=thing.height)return false;
    if(!thing.pixels[2*((size_t)row*thing.width+column)+1])return false;
    at=t;return true;
}
// Big sectors (SIGIL's have hundreds of lines) put their lines in a tree of
// boxes, so a ray tests only the lines near its path. Call after the lines
// are listed; heights may change later, the tree is 2D.
constexpr size_t bakeTreeLines=16;
inline void buildBakeTree(const BakeMap &map,BakeSector &s) {
    s.tree.clear();s.order.clear();
    if(s.lines.size()<=bakeTreeLines)return;
    for(size_t n=0;n<s.lines.size();++n)s.order.push_back((int)n);
    auto center=[&](int position,int axis) {
        const BakeLine &l=map.lines[s.lines[position]];
        return axis?l.ay+l.by:l.ax+l.bx;
    };
    // Padded, so a crossing found on a line always lies inside its box.
    auto bound=[&](BakeNode &node) {
        node.low[0]=node.low[1]=INFINITY;node.high[0]=node.high[1]=-INFINITY;
        for(int k=node.first;k<node.first+node.count;++k) {
            const BakeLine &l=map.lines[s.lines[s.order[k]]];
            node.low[0]=std::min({node.low[0],l.ax,l.bx});node.high[0]=std::max({node.high[0],l.ax,l.bx});
            node.low[1]=std::min({node.low[1],l.ay,l.by});node.high[1]=std::max({node.high[1],l.ay,l.by});
        }
        for(int axis=0;axis<2;++axis) {node.low[axis]-=0.05f;node.high[axis]+=0.05f;}
    };
    s.tree.push_back({{0,0},{0,0},0,(int)s.order.size()});
    for(size_t n=0;n<s.tree.size();++n) {
        bound(s.tree[n]);
        BakeNode node=s.tree[n];
        if(node.count<=4)continue;
        int axis=node.high[0]-node.low[0]>=node.high[1]-node.low[1]?0:1,middle=node.first+node.count/2;
        std::nth_element(s.order.begin()+node.first,s.order.begin()+middle,s.order.begin()+node.first+node.count,
                         [&](int a,int b){return center(a,axis)<center(b,axis);});
        s.tree[n].first=(int)s.tree.size();s.tree[n].count=0;
        s.tree.push_back({{0,0},{0,0},node.first,middle-node.first});
        s.tree.push_back({{0,0},{0,0},middle,node.first+node.count-middle});
    }
}
inline void buildBakeTrees(BakeMap &map) {for(auto &s:map.sectors)buildBakeTree(map,s);}
// Where a 2D ray (x,y)+direction*t is inside the box, as t from near to far;
// false when it misses.
inline bool bakeNodeSpan(const BakeNode &node,float x,float y,const float direction[3],float &near,float &far) {
    near=-INFINITY;far=INFINITY;
    const float origin[2]={x,y};
    for(int axis=0;axis<2;++axis) {
        if(std::fabs(direction[axis])<1e-12f) {
            if(origin[axis]<node.low[axis]||origin[axis]>node.high[axis])return false;
            continue;
        }
        float a=(node.low[axis]-origin[axis])/direction[axis],b=(node.high[axis]-origin[axis])/direction[axis];
        near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));
    }
    return near<=far;
}
// How a ray treats sky ceilings. Doom draws nothing above one, so a sun ray
// escapes through it, and a light ray passes over it as if it were open air.
enum class SkyCeiling { escape, open };
// True when the ray from (x,y,z) inside sector along a unit direction travels
// length without hitting a wall, floor or ceiling, or (escape) leaves through
// a sky ceiling. Upper walls under a non-sky ceiling block; crossing above a
// sky neighbor's ceiling from a sky sector does not. A blocked ray reports
// what stopped it in hit. roofless: sectors whose ceiling it takes for sky
// as well (the weather's ground bake: a porch's roof, so the ground under
// it is traced as the open ground beside it is). skyward: set when the ray
// left through the sky, and not when it only ran its length: a ray that
// started a hair outside its sector (a texel in a corner, a cell under a
// ledge's edge) crosses nothing and runs on through the void.
inline bool traceRay(const BakeMap &map,int sector,float x,float y,float z,const float direction[3],float length,SkyCeiling sky,BakeHit *hit=nullptr,
                     const std::vector<char> *roofless=nullptr,bool *skyward=nullptr) {
    float t=0;int previous=-1;
    if(skyward)*skyward=false;
    auto stop=[&](BakeHit::Kind kind,float at,int line,int part) {
        if(hit) *hit={kind,sector,line,part,x+direction[0]*at,y+direction[1]*at,z+direction[2]*at};
        return false;
    };
    auto skyOver=[&](int index){return map.sectors[index].sky||(roofless&&(size_t)index<roofless->size()&&(*roofless)[index]);};
    for(int step=0;step<256&&sector>=0;++step) {
        const BakeSector &s=map.sectors[sector];
        bool sSky=skyOver(sector);
        bool openTop=sSky&&sky==SkyCeiling::open;
        float exit=INFINITY,best=INFINITY,along=0;int crossed=-1,first=INT32_MAX;
        if(direction[2]>0&&!openTop) exit=(s.ceiling-z)/direction[2];
        if(direction[2]<0) exit=(s.floor-z)/direction[2];
        // The nearest crossing ahead; on a tie the line listed first, so
        // the tree finds the line the plain list would.
        auto test=[&](int position) {
            int index=s.lines[position];
            if(index==previous)return;
            const BakeLine &line=map.lines[index];
            float ex=line.bx-line.ax,ey=line.by-line.ay,ox=line.ax-x,oy=line.ay-y;
            float det=direction[0]*ey-direction[1]*ex;
            if(std::fabs(det)<1e-9f)return;
            float crossing=(ox*ey-oy*ex)/det,u=(ox*direction[1]-oy*direction[0])/det;
            // A ray starting on one of its sector's lines, or a hair past it (a floor cell's
            // center on a diagonal wall), crosses it at once when it heads out through it
            // (the front lies right of a to b): skipped as behind it, the wall let the light of
            // the room beyond onto the cells along its foot and top, as a lit seam (MAP02's
            // computer room wall). Heading in, the line is behind it.
            if(crossing<=t+1e-4f) {
                if(previous>=0||crossing<-0.01f||line.front==line.back)return;
                if(line.front==sector?det>=0:line.back==sector?det<=0:true)return;
            }
            if((crossing<best||(crossing==best&&position<first))&&u>=0&&u<=1)
                {best=crossing;along=u;crossed=index;first=position;}
        };
        if(s.tree.empty()) for(int n=0;n<(int)s.lines.size();++n) test(n);
        else {
            // Past the floor, ceiling or ray's end a crossing changes nothing.
            float reach=std::min(exit,length);
            int stack[64],depth=0;stack[depth++]=0;
            while(depth) {
                const BakeNode &node=s.tree[stack[--depth]];
                float near,far;
                if(!bakeNodeSpan(node,x,y,direction,near,far)||far<t||near>best||near>reach)continue;
                if(node.count) {for(int k=node.first;k<node.first+node.count;++k)test(s.order[k]);continue;}
                // Nearer child last, so it is searched first.
                float a,b,an,bn;
                bool hitA=bakeNodeSpan(s.tree[node.first],x,y,direction,an,a),hitB=bakeNodeSpan(s.tree[node.first+1],x,y,direction,bn,b);
                if(!hitA&&!hitB)continue;
                bool aFirst=!hitB||(hitA&&an<=bn);
                stack[depth++]=aFirst?node.first+1:node.first;stack[depth++]=aFirst?node.first:node.first+1;
            }
        }
        float nearest=std::min({exit,best,length}),hitThing=INFINITY,at;int struck=-1;
        for(int index:s.things)
            if(thingBlocks(map.things[index],x,y,z,direction,t,nearest,at)&&at<hitThing) {hitThing=at;struck=index;}
        if(hitThing<INFINITY)return stop(BakeHit::thing,hitThing,struck,0);
        if(length<=exit&&length<=best)return true;
        if(exit<=best) {
            if(direction[2]>0&&sSky&&sky==SkyCeiling::escape) {if(skyward)*skyward=true;return true;}
            return stop(direction[2]>0?BakeHit::ceiling:BakeHit::floor,std::max(exit,t),-1,0);
        }
        const BakeLine &line=map.lines[crossed];
        int next=line.front==sector?line.back:line.front;
        if(next<0)return stop(BakeHit::wall,best,crossed,0);
        const BakeSector &n=map.sectors[next];
        float height=z+direction[2]*best;
        if(height<n.floor)return stop(BakeHit::wall,best,crossed,2);
        if(height>n.ceiling) {
            if(!(sSky&&skyOver(next)))return stop(BakeHit::wall,best,crossed,1);
            if(sky==SkyCeiling::escape) {if(skyward)*skyward=true;return true;}
        }
        if(maskBlocks(line,along,height))return stop(BakeHit::wall,best,crossed,0);
        sector=next;previous=crossed;t=best;
    }
    return false;
}
inline bool sunReachesSky(const BakeMap &map,int sector,float x,float y,float z,const float direction[3]) {
    return traceRay(map,sector,x,y,z,direction,INFINITY,SkyCeiling::escape);
}
// The rays a point sends toward the sun, as unit directions; returns how many.
// spread 0 (a clear sky): four over a few degrees of sun disc on a rotated
// grid, a stepped five-level penumbra. Under cloud (spread up to 1, sun.cpp's
// sunSpread) the sun is a wide glow instead of a disc: sixteen on a spiral
// over a cone up to about 20 degrees round it, so a shadow keeps its hard
// foot at the wall and goes soft the further it falls from it.
// gpuSunVisibility sends the same rays.
constexpr int sunRaysMost=16;
inline int sunRays(const float direction[3],float spread,float rays[sunRaysMost][3]) {
    static const float grid[4][2]={{-0.125f,-0.375f},{0.375f,-0.125f},{0.125f,0.375f},{-0.375f,0.125f}};
    int count=spread>0?sunRaysMost:4;
    float reach=0.04f+0.36f*std::min(spread,1.0f);
    for(int n=0;n<count;++n) {
        float ox,oy;
        if(spread>0) {
            float r=std::sqrt((n+0.5f)/count)*reach,a=n*2.39996323f;
            ox=r*std::cos(a);oy=r*std::sin(a);
        }
        else {ox=grid[n][0]*0.1f;oy=grid[n][1]*0.1f;}
        float d[3]={direction[0]+ox,direction[1]+oy,direction[2]};
        float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        for(int c=0;c<3;++c)rays[n][c]=d[c]/length;
    }
    return count;
}
// Fraction of sunRays that reach the sky: a penumbra that widens with
// distance from the occluder. Rays start at the cell center so they never
// begin outside their sector.
inline float sunVisibility(const BakeMap &map,int sector,float x,float y,float z,const float direction[3],float spread=0) {
    float rays[sunRaysMost][3];
    int count=sunRays(direction,spread,rays),open=0;
    for(int n=0;n<count;++n)open+=sunReachesSky(map,sector,x,y,z,rays[n]);
    return open/(float)count;
}
// A static light with the same falloff, panel emission and facing as
// flashAtOpen and flashFacing in platform/shaders/lighting.glsl; without a
// normal (sprites) there is no facing. A glowing wall panel (extent: half its
// height, as in flashAt) is a vertical span: rays to the middles of its
// thirds count a third each, so a lip hiding part of the panel lets part of
// its light by. (nx, ny): the panel's wall normal (doom_panel_emission), 0
// for other lights. beam (with a normal): it throws its light straight out,
// falling off as the cosine to that power across, seen from above (window
// light shining out into the night). Returns the amount added (before
// color); direction, if given, receives the unit vector toward the light
// when it reaches the point.
// tail: how many radii the light reaches in the bake (0: its radius). Within
// its radius a light falls off as the dynamic lights do, to nothing at the
// radius: a shape drawn for one lamp seen up close. But light does not stop;
// a real lamp's falls with the square of the distance and never ends, and a
// ceiling of light panels 500 units across lit its own room to the brim and
// the room next door not at all, the floor cut in two on the line where the
// last panel's radius ended (E1M2's start). With a tail the light goes on
// past the radius at a small share (bakeTailShare at the radius; settings.lampTail), falling
// with the square of the distance and easing to nothing at tail radii: one
// lamp's tail is faint, a room's worth of lamps adds up to the spill a
// bright room throws into a dark one. Walls stop it as they stop the rest.
// A negative tail (whole-level lighting, PLAN-light-model.md): minus the
// distance the light reaches, and it falls the whole way as light does, with
// the square of the distance from a source about three eighths of its radius
// across (bakeWholeCore), set so that a lamp is about as bright as it was at
// half its radius (bakeWholeGain), easing to nothing only at the reach so the
// bake can stop somewhere. No shape of its own per lamp, no edge where one
// lamp ends.
// tiles, tileCount: the glowing floor or ceiling tiles a merged lamp stands
// for (x, y, z each; whole-level lighting). A ceiling of light panels is
// many small lights, and behind a lintel each throws its own sharp shadow at
// its own place: together a fan of edges, the light going step by step, not
// the one hard line a single lamp at their middle drew. The lamp's light is
// still measured from its middle; how much of it arrives is the share of
// its tiles the point sees.
// post: a lamp on a post (a glowing tube or globe on a dark stand, cap on top). Round about it
// shines evenly, but not up and down: a tube's side gives the most straight out and nothing
// along its own length, its foot and stand keep the light off the floor right under it, its
// cap off the ceiling above. So the light lands as a ring on the floor round a dark foot and
// most of it on the walls, where as a bare point the floor at its foot was the brightest
// place in the room. Baked only: 0 none, 1 in full.
struct BakeLight { float x,y,z,radius,strength,directionality; float color[3]; float extent=0,nx=0,ny=0,beam=0,tail=0;
                   const float *tiles=nullptr; int tileCount=0; float post=0; };
// The share of a post lamp's light that leaves toward a point rise above it (the sine of the
// angle over the horizontal, -1 straight down to 1 straight up): the tube's side by the cosine,
// over what the stand (down) and the cap (up) still let by, and a fifth more straight out, so
// the lamp as a whole gives about what the bare point gave.
inline float bakePostShare(float rise) {
    float side=std::sqrt(std::max(0.0f,1-rise*rise)),least=rise<0?0.12f:0.3f;
    return (least+(1-least)*side)*1.2f;
}
// Whether the wall line surely stops the unit ray v of that length from (x,y,z): it crosses the
// line well inside its ends, clear of the ray's own, and there the line has no sector behind it
// or the ray passes under the floor or over the ceiling of the one it has (as traceRay stops it).
// Sure only: a crossing near an end, a height near an edge or a masked middle is left to the trace.
inline bool bakeLineStops(const BakeMap &map,int index,float x,float y,float z,const float v[3],float length) {
    if(index<0||index>=(int)map.lines.size())return false;
    const BakeLine &line=map.lines[index];
    if(line.front==line.back)return false;
    float ex=line.bx-line.ax,ey=line.by-line.ay,ox=line.ax-x,oy=line.ay-y;
    float det=v[0]*ey-v[1]*ex;
    if(std::fabs(det)<1e-4f)return false;
    float crossing=(ox*ey-oy*ex)/det,u=(ox*v[1]-oy*v[0])/det;
    if(!(crossing>0.05f&&crossing<length-0.05f&&u>0.002f&&u<0.998f))return false;
    // The front lies right of a to b: heading out through it, det is negative.
    int from=det<0?line.front:line.back,next=det<0?line.back:line.front;
    if(from<0)return false;
    if(next<0)return true;
    const BakeSector &n=map.sectors[next];
    float height=z+v[2]*crossing;
    if(height<n.floor-0.01f)return true;
    return height>n.ceiling+0.01f&&!(map.sectors[from].sky&&n.sky);
}
// Whether (x,y) lies inside the sector, by its lines (even-odd, a ray towards +x). A texel's
// place can lie a hair outside its sector (a cell's middle past a diagonal wall, a strip's end in
// a corner); from there the trace crosses nothing and runs on through the void.
inline bool bakeInsideSector(const BakeMap &map,int sector,float x,float y) {
    if(sector<0||sector>=(int)map.sectors.size())return false;
    bool inside=false;
    for(int index:map.sectors[sector].lines) {
        const BakeLine &line=map.lines[index];
        if(line.front==line.back||(line.ay>y)==(line.by>y))continue;
        if(line.ax+(y-line.ay)/(line.by-line.ay)*(line.bx-line.ax)>x)inside=!inside;
    }
    return inside;
}
// A light's ray, as traceRay(..., SkyCeiling::open) answers it. blocker, if given, is the wall
// that stopped the ray before this one (-1: none yet): most rays that do not arrive are stopped
// by the wall that stopped their neighbour (a lamp's other tiles from the same texel, the same
// lamp from the next texel), and that is a few multiplications to see, where the trace walks the
// map. It takes the wall that stops this one.
#ifdef DOOM_DEVTOOLS
// DERP_RAYCOUNT (bake_lights.cpp): the rays of the light-texel pair being baked on this thread,
// how many arrived, and how many the last wall stopped without a trace.
inline bool bakeRayCounting=false;
inline thread_local int bakePairRays=0,bakePairOpen=0,bakePairQuick=0;
#endif
inline bool bakeRayOpen(const BakeMap &map,int sector,float x,float y,float z,const float v[3],float length,int *blocker) {
    bool open;
    if(!blocker)open=traceRay(map,sector,x,y,z,v,length,SkyCeiling::open);
    else if(bakeLineStops(map,*blocker,x,y,z,v,length)) {
        open=false;
#ifdef DOOM_DEVTOOLS
        bakePairQuick+=bakeRayCounting;
#endif
    } else {
        BakeHit hit;
        open=traceRay(map,sector,x,y,z,v,length,SkyCeiling::open,&hit);
        if(!open&&hit.kind==BakeHit::wall)*blocker=hit.line;
    }
#ifdef DOOM_DEVTOOLS
    if(bakeRayCounting) {++bakePairRays;bakePairOpen+=open;}
#endif
    return open;
}
constexpr float bakeWholeCore=0.375f,bakeWholeGain=1.4f;
inline float bakeTailShare=0.15f;   // set by the bake from settings.lampTail
inline float bakeReach(const BakeLight &light) {return light.tail<0?-light.tail:light.tail>1?light.radius*light.tail:light.radius;}
inline float addBakedLight(const BakeMap &map,const BakeLight &light,int sector,float x,float y,float z,const float normal[3],float out[3],float *direction=nullptr,int *blocker=nullptr) {
    float d[3]={light.x-x,light.y-y,light.z-z};
    float distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    float reach=bakeReach(light);
    if(distance>=reach)return 0;
    float falloff=std::max(0.0f,1-distance/light.radius),amount=light.strength*falloff*falloff*(3-2*falloff);
    if(light.tail<0) {
        float q=distance/(light.radius*bakeWholeCore),far=1-distance/reach;
        amount=light.strength*bakeWholeGain/(1+q*q)*far*far;
    } else if(light.tail>1) {
        float near=light.radius/std::max(distance,light.radius*0.5f),far=1-distance/reach;
        amount=std::max(amount,light.strength*bakeTailShare*near*near*far*far);
    }
    amount*=doom_panel_emission(-d[0],-d[1],-d[2],light.nx,light.ny,normal);
    if(light.post>0&&distance>0.001f)amount*=1+light.post*(bakePostShare(-d[2]/distance)-1);
    if(light.beam>0) {
        float across=std::sqrt(d[0]*d[0]+d[1]*d[1]);
        amount*=across>0.001f?std::pow(std::max(0.0f,-(d[0]*light.nx+d[1]*light.ny)/across),light.beam):1.0f;
    }
    float facing=1;
    if(distance>0.001f) {
        for(float &c:d)c/=distance;
        if(normal)facing=1+doom_near_directionality(light.directionality,distance)*(std::max(0.0f,normal[0]*d[0]+normal[1]*d[1]+normal[2]*d[2])-1);
        if(amount*facing<1/512.0f)return 0;
        // Stop just short of the light so its own surface never blocks it.
        if(light.tileCount>1&&light.extent<=0) {
            int open=0;
            for(int k=0;k<light.tileCount;++k) {
                float v[3]={light.tiles[3*k]-x,light.tiles[3*k+1]-y,light.tiles[3*k+2]-z};
                float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
                if(length<=0.5f) {++open;continue;}
                for(float &c:v)c/=length;
                open+=bakeRayOpen(map,sector,x,y,z,v,length-0.5f,blocker);
            }
            if(!open)return 0;
            amount*=open/(float)light.tileCount;
        } else if(light.extent<=0) {if(!bakeRayOpen(map,sector,x,y,z,d,distance-0.5f,blocker))return 0;}
        else {
            int open=0;
            for(int k=-1;k<=1;++k) {
                float v[3]={light.x-x,light.y-y,light.z+k*light.extent*(2/3.0f)-z};
                float length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
                if(length<=0.5f) {++open;continue;}
                for(float &c:v)c/=length;
                open+=bakeRayOpen(map,sector,x,y,z,v,length-0.5f,blocker);
            }
            if(!open)return 0;
            amount*=open/3.0f;
        }
    } else d[0]=d[1]=0,d[2]=1;
    for(int c=0;c<3;++c)out[c]+=light.color[c]*amount*facing;
    if(direction)for(int c=0;c<3;++c)direction[c]=d[c];
    return amount*facing;
}
// An emissive liquid floor (or ceiling) lit as one light, so a pool glows as
// a whole instead of as a grid of points: light comes from the nearest point
// of its convex pieces (BSP leaves), lifted off the surface like the point
// lights that would otherwise tile it. Only the side it faces receives light.
// Its glow spills over a pit's rim: rays test against a point clearance
// above the surface (below, for ceilings) rather than the lifted point.
struct BakeArea {
    std::vector<std::vector<std::array<float,2>>> pieces;
    float z=0,clearance=48; bool ceiling=false;
    float radius=256,strength=1,directionality=0.35f; float color[3]={1,1,1};
    float low[2]={1e9f,1e9f},high[2]={-1e9f,-1e9f}; // Bounds of the pieces.
    // Whole-level lighting: points spread over the area (x, y). How much of the area's light a
    // point gets is the share of them it sees. Without them (classic) one ray to the nearest
    // point of the area decides, and two more further in: through a grate or past a ledge that
    // one ray flipped from texel to texel, and the pool's light lay on the ceiling as a pattern.
    std::vector<std::array<float,2>> samples;
};
constexpr float areaLift=8;
// Squared distance in the plane from (x,y) to the area (0 inside), and the
// nearest point. Pieces may wind either way.
inline float areaNearest(const BakeArea &area,float x,float y,float &nx,float &ny) {
    float best=INFINITY;nx=x;ny=y;
    for(const auto &piece:area.pieces) {
        size_t n=piece.size();if(n<3)continue;
        bool positive=false,negative=false;float local=INFINITY,px=x,py=y;
        for(size_t i=0;i<n;++i) {
            const auto &a=piece[i],&b=piece[(i+1)%n];
            float ex=b[0]-a[0],ey=b[1]-a[1],cross=ex*(y-a[1])-ey*(x-a[0]);
            positive|=cross>0.01f;negative|=cross<-0.01f;
            float length2=ex*ex+ey*ey,t=length2>0?std::clamp(((x-a[0])*ex+(y-a[1])*ey)/length2,0.0f,1.0f):0;
            float qx=a[0]+ex*t,qy=a[1]+ey*t,d=(x-qx)*(x-qx)+(y-qy)*(y-qy);
            if(d<local) {local=d;px=qx;py=qy;}
        }
        if(!(positive&&negative)) {nx=x;ny=y;return 0;}
        if(local<best) {best=local;nx=px;ny=py;}
    }
    return best;
}
// Like addBakedLight for an area. A ray to the nearest point can graze the
// pool's rim, so points 32 and 96 units further in light what sees past it;
// the first one that reaches the point counts. inside, if given, tells
// whether the point lies over the area itself (the pool's own surface).
inline float addBakedArea(const BakeMap &map,const BakeArea &area,int sector,float x,float y,float z,const float normal[3],float out[3],float *direction=nullptr,bool *inside=nullptr,int *blocker=nullptr) {
    if(inside)*inside=false;
    if((area.ceiling?area.z-z:z-area.z)<=0)return 0;
    float bx=std::max({area.low[0]-x,0.0f,x-area.high[0]}),by=std::max({area.low[1]-y,0.0f,y-area.high[1]});
    if(bx*bx+by*by>=area.radius*area.radius)return 0;
    float nx,ny,plane=std::sqrt(areaNearest(area,x,y,nx,ny));
    if(plane>=area.radius)return 0;
    float ux=plane>0.001f?(nx-x)/plane:0,uy=plane>0.001f?(ny-y)/plane:0;
    float ez=area.z+(area.ceiling?-areaLift:areaLift);
    for(float reach:{0.0f,32.0f,96.0f}) {
        if(reach>0&&plane<=0.001f)break;
        float tx=nx+ux*reach,ty=ny+uy*reach,ix,iy;
        if(reach>0&&areaNearest(area,tx,ty,ix,iy)>0)break;
        float d[3]={tx-x,ty-y,ez-z},distance=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(distance>=area.radius)break;
        float falloff=1-distance/area.radius,amount=area.strength*falloff*falloff*(3-2*falloff),facing=1;
        if(!area.samples.empty()) {
            // The share of the area in view, measured from its nearest point as before.
            if(distance>0.001f) {
                for(float &c:d)c/=distance;
                if(normal)facing=1+area.directionality*(std::max(0.0f,normal[0]*d[0]+normal[1]*d[1]+normal[2]*d[2])-1);
            } else d[0]=d[1]=0,d[2]=area.ceiling?-1.0f:1.0f;
            if(amount*facing<1/512.0f)return 0;
            float tz=area.z+(area.ceiling?-1:1)*std::max(area.clearance,areaLift);
            int open=0;
            for(const auto &p:area.samples) {
                float v[3]={p[0]-x,p[1]-y,tz-z},length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
                if(length<=0.5f) {++open;continue;}
                for(float &c:v)c/=length;
                open+=bakeRayOpen(map,sector,x,y,z,v,length-0.5f,blocker);
            }
            if(!open)return 0;
            amount*=open/(float)area.samples.size();
            if(normal&&std::fabs(normal[2])<=0.5f)amount=std::min(amount,area.strength*0.5f);
            for(int c=0;c<3;++c)out[c]+=area.color[c]*amount*facing;
            if(direction)for(int c=0;c<3;++c)direction[c]=d[c];
            if(inside)*inside=plane<=0.001f;
            return amount*facing;
        }
        if(distance>0.001f) {
            for(float &c:d)c/=distance;
            if(normal)facing=1+area.directionality*(std::max(0.0f,normal[0]*d[0]+normal[1]*d[1]+normal[2]*d[2])-1);
            if(amount*facing<1/512.0f)continue;
            float tz=area.z+(area.ceiling?-1:1)*std::max(area.clearance,areaLift);
            float v[3]={tx-x,ty-y,tz-z},length=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
            for(float &c:v)c/=std::max(length,0.001f);
            if(length>0.5f&&!traceRay(map,sector,x,y,z,v,length-0.5f,SkyCeiling::open))continue;
        } else d[0]=d[1]=0,d[2]=area.ceiling?-1.0f:1.0f;
        // A wall sees the pool over at most half its view, so it never takes
        // more than half of what the pool's own surface does: at a rim it
        // would otherwise glow brighter than the nukage lighting it.
        if(normal&&std::fabs(normal[2])<=0.5f)amount=std::min(amount,area.strength*0.5f);
        for(int c=0;c<3;++c)out[c]+=area.color[c]*amount*facing;
        if(direction)for(int c=0;c<3;++c)direction[c]=d[c];
        if(inside)*inside=plane<=0.001f;
        return amount*facing;
    }
    return 0;
}
// A sector's light level already shows the lamps its mapper lit it for, so
// static light only fills the headroom up to bakeLimit, the brightest a
// lamp-lit surface gets: in full up to half the headroom, then easing toward
// the limit. Glowing panels lift a 128 room to about 1.25 instead of the
// clamp, a hallway already at 224 barely rises, and a torch's moderate glow
// in a dark room stays nearly as it was. The bake stores the full light;
// world.frag and the readers here apply this with the sector's current light.
constexpr float bakeLimit=1.25f;
inline float bakeHeadroom(float sectorLight) {return std::max(bakeLimit-sectorLight,0.0f);}
inline float bakeShoulder(float light,float headroom) {
    float knee=headroom*0.5f,rest=headroom-knee;
    if(light<=knee||rest<=0)return std::min(light,knee);
    return knee+rest*(1-std::exp(-(light-knee)/rest));
}
// Static light on a surface in a sector of that light level: its brightness
// (luminance) follows bakeShoulder and its channels keep their ratios. A
// colored lamp adds little brightness, so it still tints a bright sector, as
// under E1M1's red-lit monitor boxes, where the sector stands at 255.
inline float bakeLuminance(const float rgb[3]) {return 0.299f*rgb[0]+0.587f*rgb[1]+0.114f*rgb[2];}
inline void fillHeadroom(float rgb[3],float sectorLight) {
    float luminance=bakeLuminance(rgb);
    if(luminance<=0)return;
    float scale=bakeShoulder(luminance,bakeHeadroom(sectorLight))/luminance;
    for(int c=0;c<3;++c)rgb[c]*=scale;
}
// A sector's light level stands for the lamps the mapper lit it with, so the
// static light (full, before fillHeadroom) colors it by its share: the
// multiplier for the sector light of a point lit at that level. White lamps
// leave it white. Bounce light, weighted by bounceTint, counts too: the
// lamps' light bounced around the room colors corners they never reach
// directly (E1M2's dark room, sector 120). Keep lampTint in world.frag
// equivalent. The shader then brings the tint back up to the untinted
// brightness (paling a tint too dark to bring up by 1.6), for surfaces and for the tint sprites pack
// from here alike: bytes hold no more than 1.
inline void lampTint(const float statics[3],const float bounce[3],float bounceTint,float light,float out[3]) {
    float colored[3];
    for(int c=0;c<3;++c)colored[c]=statics[c]+bounce[c]*bounceTint;
    float peak=std::max({colored[0],colored[1],colored[2]});
    float share=peak>0?peak/(peak+std::max(light,0.0f)):0;
    for(int c=0;c<3;++c)out[c]=1+(peak>0?colored[c]/peak-1:0)*share;
}
// Bounce gathering direction around a unit normal: (u,v) in [0,1)^2 maps to
// a cosine-weighted hemisphere, so averaging what the rays hit is the
// irradiance up to albedo.
inline void cosineDirection(const float normal[3],float u,float v,float out[3]) {
    float radius=std::sqrt(u),angle=6.28318531f*v;
    float a=radius*std::cos(angle),b=radius*std::sin(angle),c=std::sqrt(std::max(0.0f,1-u));
    float helper[3]={std::fabs(normal[2])<0.9f?0.0f:1.0f,0,std::fabs(normal[2])<0.9f?1.0f:0.0f};
    float tangent[3]={helper[1]*normal[2]-helper[2]*normal[1],helper[2]*normal[0]-helper[0]*normal[2],helper[0]*normal[1]-helper[1]*normal[0]};
    float length=std::sqrt(tangent[0]*tangent[0]+tangent[1]*tangent[1]+tangent[2]*tangent[2]);
    for(float &t:tangent)t/=length;
    float bitangent[3]={normal[1]*tangent[2]-normal[2]*tangent[1],normal[2]*tangent[0]-normal[0]*tangent[2],normal[0]*tangent[1]-normal[1]*tangent[0]};
    for(int i=0;i<3;++i)out[i]=tangent[i]*a+bitangent[i]*b+normal[i]*c;
}
#endif
