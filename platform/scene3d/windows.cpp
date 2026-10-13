/* Window glass: windows found in the map's geometry get a pane of glass
   (glass.frag), which shatters into falling shards the first time a shot,
   projectile or blast goes through it. Render-only: the engine never sees
   the glass, shots and sight pass as in vanilla, and every level starts
   with whole panes. */
#include "internal.h"

namespace doom3d {
std::vector<Vertex> glassVertices;
bool glassMirrorActive=false;
float glassMirrorPlane[4]={};
// A pane fills the opening of one line between a window's frame sector and
// the sector it looks out on (outer); inner is the room on the other side.
// The panes of one frame break together.
struct GlassPane { int line,frame,outer,inner; float ax,ay,bx,by; uint32_t seed; bool broken=false; };
// A shard moves at the tic rate (units per tic), from (px,py,pz) last tic.
struct Shard { float x,y,z,px,py,pz,vx,vy,vz,size,tall; int pane,die; uint32_t seed; bool landed=false; };
struct MissileWatch { float x,y,z; bool missile,seen; int type; };
static std::vector<GlassPane> panes;
static std::vector<int> paneOfLine;
// Per frame sector with panes, the lines near its window that stop shards.
static std::unordered_map<int,std::vector<int>> nearLines;
static std::vector<Shard> shards;
static std::unordered_map<const mobj_t*,MissileWatch> missileWatch;
static unsigned glassLevelSerial=~0u;

static float rawFloor(const sector_t *s) {return units(s->floorheight);}
static float rawCeiling(const sector_t *s) {return units(s->ceilingheight);}
static bool skySector(const sector_t *s) {return s->ceilingpic==skyflatnum;}
// Doors that open the sector behind them without a tag.
static bool manualDoor(int special) {
    switch(special) {case 1:case 26:case 27:case 28:case 31:case 32:case 33:case 34:case 117:case 118:return true;}
    return false;
}
static std::array<float,4> sectorBox(const sector_t &s) {
    std::array<float,4> box={1e9f,1e9f,-1e9f,-1e9f};
    for(int n=0;n<s.linecount;++n)for(const vertex_t *v:{s.lines[n]->v1,s.lines[n]->v2}) {
        box[0]=std::min(box[0],units(v->x));box[1]=std::min(box[1],units(v->y));
        box[2]=std::max(box[2],units(v->x));box[3]=std::max(box[3],units(v->y));
    }
    return box;
}
// A window is a thin frame sector between two others, with a sill and a
// lintel: its floor above both neighbors and its ceiling below them (or
// under the sky beside it), too high a step to walk in or behind an
// impassable line, at least 32 units wide and tall. Sectors that move or
// that line specials act on are left alone: a lift is not a window. The
// glass goes on the side facing the sky, or else the larger room.
static void findWindows() {
    panes.clear();paneOfLine.assign(numlines,-1);nearLines.clear();
    std::set<int> tags;
    for(int i=0;i<numlines;++i)if(lines[i].special&&lines[i].tag)tags.insert(lines[i].tag);
    std::vector<char> excluded(numsectors,0);
    for(int i=0;i<numsectors;++i)if(sectors[i].tag&&tags.count(sectors[i].tag))excluded[i]=1;
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.special||!line.frontsector||!line.backsector)continue;
        excluded[line.frontsector-sectors]=excluded[line.backsector-sectors]=1;
        if(manualDoor(line.special))excluded[line.backsector-sectors]=1;
    }
    std::vector<float> area(numsectors);
    for(int i=0;i<numsectors;++i) {auto b=sectorBox(sectors[i]);area[i]=(b[2]-b[0])*(b[3]-b[1]);}
    for(int s=0;s<numsectors;++s) {
        const sector_t &frame=sectors[s];
        if(excluded[s]||skySector(&frame)||rawCeiling(&frame)-rawFloor(&frame)<32)continue;
        bool window=true;std::vector<int> sides;
        for(int n=0;n<frame.linecount&&window;++n) {
            const line_t *line=frame.lines[n];
            if(!line->frontsector||!line->backsector)continue;
            const sector_t *other=line->frontsector==&frame?line->backsector:line->frontsector;
            if(other==&frame)continue;
            float bottom=std::max(rawFloor(&frame),rawFloor(other)),top=std::min(rawCeiling(&frame),rawCeiling(other));
            if(top-bottom<16)continue;
            bool walkable=rawFloor(&frame)-rawFloor(other)<=24&&top-bottom>=56&&!(line->flags&ML_BLOCKING);
            bool lintel=rawCeiling(&frame)<rawCeiling(other)||skySector(other);
            if(walkable||!lintel||rawFloor(&frame)<=rawFloor(other)) {window=false;break;}
            int o=int(other-sectors);
            if(std::find(sides.begin(),sides.end(),o)==sides.end())sides.push_back(o);
        }
        if(!window||sides.size()<2)continue;
        auto box=sectorBox(frame);
        float thin=std::min(box[2]-box[0],box[3]-box[1]),wide=std::max(box[2]-box[0],box[3]-box[1]);
        if(thin>64||wide<32)continue;
        auto rank=[&](int o){return std::make_pair(skySector(&sectors[o]),area[o]);};
        int outer=*std::max_element(sides.begin(),sides.end(),[&](int a,int b){return rank(a)<rank(b);}),inner=-1;
        for(int o:sides)if(o!=outer&&(inner<0||area[o]>area[inner]))inner=o;
        for(int n=0;n<frame.linecount;++n) {
            const line_t *line=frame.lines[n];
            if(!line->frontsector||!line->backsector)continue;
            const sector_t *other=line->frontsector==&frame?line->backsector:line->frontsector;
            if(other-sectors!=outer||paneOfLine[line-lines]>=0)continue;
            int index=int(line-lines);
            paneOfLine[index]=(int)panes.size();
            panes.push_back({index,s,outer,inner,units(line->v1->x),units(line->v1->y),units(line->v2->x),units(line->v2->y),hash32((uint32_t)index*2654435761u+7)});
        }
    }
    // Lines within reach of each window, for the shards that fly from it.
    for(const GlassPane &p:panes) {
        auto &list=nearLines[p.frame];
        if(!list.empty())continue;
        float cx=(p.ax+p.bx)/2,cy=(p.ay+p.by)/2;
        for(int i=0;i<numlines;++i) {
            float ax=units(lines[i].v1->x),ay=units(lines[i].v1->y),ex=units(lines[i].v2->x)-ax,ey=units(lines[i].v2->y)-ay;
            float t=std::clamp(((cx-ax)*ex+(cy-ay)*ey)/std::max(ex*ex+ey*ey,1e-6f),0.0f,1.0f);
            if(std::hypot(ax+ex*t-cx,ay+ey*t-cy)<256)list.push_back(i);
        }
    }
    fprintf(stderr,"3D glass: %zu window panes.\n",panes.size());
}
static void paneHeights(const GlassPane &p,float &bottom,float &top) {
    const sector_t *frame=&sectors[p.frame],*outer=&sectors[p.outer];
    bottom=std::max(floorZ(frame),floorZ(outer));top=std::min(ceilZ(frame),ceilZ(outer));
}
// Breaks every pane of a window. Shards scatter from all over the glass:
// fast along the shot near where it went through, slower further away, a few
// back toward the shooter; they fall, bounce once and lie glinting a while.
static void shatter(int frame,float hx,float hy,float hz,float dx,float dy) {
    float length=std::hypot(dx,dy);
    if(length>1e-4f) {dx/=length;dy/=length;}
    for(GlassPane &p:panes) {
        if(p.frame!=frame||p.broken)continue;
        p.broken=true;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(len<1||top<=bottom)continue;
        ex/=len;ey/=len;
        // A blast in the glass's own plane pushes straight through it.
        if(length<=1e-4f) {dx=ey;dy=-ex;}
        int count=std::clamp((int)(len*(top-bottom)/36),16,160);
        if(shards.size()+count>900)shards.erase(shards.begin(),shards.begin()+std::min(shards.size(),shards.size()+count-900));
        for(int n=0;n<count;++n) {
            float along=randomRange(0,len),z=randomRange(bottom+1,top-1);
            float x=p.ax+ex*along,y=p.ay+ey*along;
            float near=1-0.65f*std::min(1.0f,std::hypot(std::hypot(x-hx,y-hy),z-hz)/96);
            float push=randomRange(0,1)<0.18f?-randomRange(0.3f,1.0f):randomRange(1.0f,3.6f)*near;
            float slide=randomRange(-0.8f,0.8f);
            Shard s={x+dx*0.5f,y+dy*0.5f,z,0,0,0,dx*push+ex*slide,dy*push+ey*slide,randomRange(-0.4f,1.6f),
                     randomRange(0.7f,2.1f),randomRange(1.0f,2.6f),int(&p-panes.data()),leveltime+(int)randomRange(14*TICRATE,24*TICRATE),
                     (uint32_t)impactRandom()};
            s.px=s.x;s.py=s.y;s.pz=s.z;
            shards.push_back(s);
        }
    }
}
static bool glassReady() {return settings.windowGlass&&glassLevelSerial==r_levelserial&&gamestate==GS_LEVEL;}
void glassShot(line_t *line,fixed_t x,fixed_t y,fixed_t z,fixed_t dx,fixed_t dy) {
    if(!line||!glassReady())return;
    int pane=paneOfLine[line-lines];
    if(pane<0||panes[pane].broken)return;
    shatter(panes[pane].frame,units(x),units(y),units(z),units(dx),units(dy));
}
// Explosions break windows within radius of the blast.
void glassBlast(float x,float y,float z,float radius) {
    if(!glassReady())return;
    for(const GlassPane &p:panes) {
        if(p.broken)continue;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay;
        float t=std::clamp(((x-p.ax)*ex+(y-p.ay)*ey)/std::max(ex*ex+ey*ey,1e-6f),0.0f,1.0f);
        float px=p.ax+ex*t,py=p.ay+ey*t,pz=std::clamp(z,bottom,top);
        if(std::hypot(std::hypot(px-x,py-y),pz-z)<radius)shatter(p.frame,px,py,pz,px-x,py-y);
    }
}
// Whether a line stops a shard crossing it at height z.
static bool stopsShard(const line_t &line,float z) {
    if(!line.frontsector||!line.backsector)return true;
    return z<std::max(rawFloor(line.frontsector),rawFloor(line.backsector))||z>std::min(rawCeiling(line.frontsector),rawCeiling(line.backsector));
}
static bool crosses(float ax,float ay,float bx,float by,const line_t &line,float &t) {
    float lx=units(line.v1->x),ly=units(line.v1->y),ex=units(line.v2->x)-lx,ey=units(line.v2->y)-ly;
    float dx=bx-ax,dy=by-ay,det=dx*ey-dy*ex;
    if(std::abs(det)<1e-6f)return false;
    float ox=lx-ax,oy=ly-ay;t=(ox*ey-oy*ex)/det;
    float u=(ox*dy-oy*dx)/det;
    return t>=0&&t<=1&&u>=0&&u<=1;
}
static void stepShards() {
    for(Shard &s:shards) {
        s.px=s.x;s.py=s.y;s.pz=s.z;
        if(s.landed)continue;
        float nx=s.x+s.vx,ny=s.y+s.vy,nz=s.z+s.vz;
        s.vz-=0.8f;
        float t;
        auto near=nearLines.find(panes[s.pane].frame);
        if(near!=nearLines.end())for(int i:near->second)
            if(crosses(s.x,s.y,nx,ny,lines[i],t)&&stopsShard(lines[i],s.z+(nz-s.z)*t)) {nx=s.x;ny=s.y;s.vx=s.vy=0;break;}
        const sector_t *sector=R_PointInSubsector((fixed_t)(nx*FRACUNIT),(fixed_t)(ny*FRACUNIT))->sector;
        float floor=rawFloor(sector),ceiling=rawCeiling(sector);
        if(nz>ceiling-1) {nz=ceiling-1;s.vz=std::min(s.vz,0.0f);}
        if(nz<=floor) {
            size_t index=sector-sectors;
            if(index<sectorMist.size()&&sectorMist[index].liquid) {s.die=leveltime;continue;} // Sinks.
            nz=floor;
            if(s.vz<-2.5f) {s.vz=-s.vz*0.3f;s.vx*=0.5f;s.vy*=0.5f;}
            else {s.vz=0;s.vx=s.vy=0;s.landed=true;}
        }
        s.x=nx;s.y=ny;s.z=nz;
    }
    shards.erase(std::remove_if(shards.begin(),shards.end(),[](const Shard &s){return leveltime>=s.die;}),shards.end());
}
// Before each tic: projectiles that went through a pane since the last one
// break it, rockets that burst near one too, and the shards move.
void watchGlass() {
    if(!settings.windowGlass||gamestate!=GS_LEVEL) return;
    if(glassLevelSerial!=r_levelserial) {
        glassLevelSerial=r_levelserial;shards.clear();missileWatch.clear();
        findWindows();
    }
    stepShards();
    if(panes.empty())return;
    for(auto &entry:missileWatch)entry.second.seen=false;
    for(int i=0;i<numsectors;++i)for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
        bool missile=thing->flags&MF_MISSILE;
        auto found=missileWatch.find(thing);
        if(!missile&&found==missileWatch.end())continue;
        float x=units(thing->x),y=units(thing->y),z=units(thing->z)+units(thing->height)/2;
        if(found==missileWatch.end()||found->second.type!=thing->type||
           std::hypot(x-found->second.x,y-found->second.y)>units(thing->info->speed)*2+16) {
            if(missile)missileWatch[thing]={x,y,z,true,true,thing->type};
            else missileWatch.erase(found);
            continue;
        }
        MissileWatch &watch=found->second;watch.seen=true;
        if(watch.missile)for(const GlassPane &p:panes) {
            if(p.broken)continue;
            float t;
            if(!crosses(watch.x,watch.y,x,y,lines[p.line],t))continue;
            float bottom,top;paneHeights(p,bottom,top);
            float at=watch.z+(z-watch.z)*t;
            if(at<bottom-2||at>top+2)continue;
            shatter(p.frame,watch.x+(x-watch.x)*t,watch.y+(y-watch.y)*t,at,x-watch.x,y-watch.y);
        }
        if(watch.missile&&!missile&&thing->type==MT_ROCKET)glassBlast(x,y,z,96);
        watch.x=x;watch.y=y;watch.z=z;watch.missile=missile;
        if(!missile)watch.seen=false; // Burst: no longer watched.
    }
    for(auto it=missileWatch.begin();it!=missileWatch.end();) it=it->second.seen?std::next(it):missileWatch.erase(it);
}
// The panes as the camera sees them, and the shards. Each pane draws only
// its face toward the eye, carrying that side's light: the outer face shows
// the outdoors' light and reflects the sky, the inner one the room's.
// Vertex: uv the texel on the pane, (sunU, sunV) its size, (red, green) the
// face normal, blue 1 on a face out under the sky; statics the pane's seed,
// top bit set once broken; lightMask[0] 1 on panes in the mirror's plane.
// Glass reflections (settings.glassReflections): the pane that fills the
// most of the view within 512 units, whole and in front of the camera,
// becomes the mirror the world is drawn again through (glassMirrorPlane:
// its normal toward the eye and offset). Panes in the same plane share it.
static void chooseMirror(const Uniforms &camera) {
    glassMirrorActive=false;
    if(!settings.glassReflections||camera.effects[2]!=0||(profile.skip&I_RENDER3D_SKIP_REFLECTIONS))return;
    float best=0;
    for(const GlassPane &p:panes) {
        if(p.broken)continue;
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(top-bottom<1||len<1)continue;
        float nx=ey/len,ny=-ex/len,facing=(camera.eye[0]-p.ax)*nx+(camera.eye[1]-p.ay)*ny;
        if(facing<0) {nx=-nx;ny=-ny;facing=-facing;}
        if(facing<1)continue;
        float t=std::clamp(((camera.eye[0]-p.ax)*ex+(camera.eye[1]-p.ay)*ey)/(len*len),0.0f,1.0f);
        float cx=p.ax+ex*t,cy=p.ay+ey*t,cz=std::clamp(camera.eye[2],bottom,top);
        float distance=std::hypot(std::hypot(cx-camera.eye[0],cy-camera.eye[1]),cz-camera.eye[2]);
        if(distance>512)continue;
        bool ahead=false;
        for(float u:{0.0f,1.0f})for(float z:{bottom,top})
            ahead|=(p.ax+ex*u-camera.eye[0])*camera.forward[0]+(p.ay+ey*u-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2]>0;
        if(!ahead)continue;
        float score=len*(top-bottom)*(facing/std::max(distance,1.0f))/std::max(distance*distance,256.0f);
        if(score>best) {best=score;glassMirrorActive=true;glassMirrorPlane[0]=nx;glassMirrorPlane[1]=ny;glassMirrorPlane[2]=0;glassMirrorPlane[3]=nx*p.ax+ny*p.ay;}
    }
}
void buildGlass(const Uniforms &camera) {
    glassVertices.clear();glassMirrorActive=false;
    if(!settings.windowGlass||glassLevelSerial!=r_levelserial)return;
    chooseMirror(camera);
    for(const GlassPane &p:panes) {
        float bottom,top;paneHeights(p,bottom,top);
        float ex=p.bx-p.ax,ey=p.by-p.ay,len=std::hypot(ex,ey);
        if(top-bottom<1||len<1)continue;
        float nx=ey/len,ny=-ex/len; // Toward the line's front side.
        float facing=(camera.eye[0]-p.ax)*nx+(camera.eye[1]-p.ay)*ny;
        if(std::abs(facing)<0.01f)continue;
        if(facing<0) {nx=-nx;ny=-ny;}
        bool frontEye=facing>0;
        const sector_t *eyeSide=frontEye?lines[p.line].frontsector:lines[p.line].backsector;
        bool outside=eyeSide-sectors==p.outer;
        const sector_t *lit=outside?&sectors[p.outer]:p.inner>=0?&sectors[p.inner]:&sectors[p.frame];
        float light=camera.effects[2]!=0?1:lighting(lit);
        // u runs along (-ny, nx), so the shader knows the pane's direction.
        Point a={p.ax,p.ay},b={p.bx,p.by};
        if((b.x-a.x)*-ny+(b.y-a.y)*nx<0)std::swap(a,b);
        unsigned statics=(p.seed&0x7fffffffu)|(p.broken?0x80000000u:0u);
        bool mirror=glassMirrorActive&&nx*glassMirrorPlane[0]+ny*glassMirrorPlane[1]>0.999f&&std::abs(nx*p.ax+ny*p.ay-glassMirrorPlane[3])<1;
        auto vertex=[&](Point at,float u,float z) {
            Vertex v={at.x,at.y,z,u,z-bottom,light,0};v.lightMask[0]=mirror?1u:0u;
            v.red=nx;v.green=ny;v.blue=outside&&skySector(&sectors[p.outer])?1.0f:0.0f;
            v.sunU=len;v.sunV=top-bottom;v.statics=statics;return v;
        };
        quad(glassVertices,vertex(a,0,top),vertex(b,len,top),vertex(b,len,bottom),vertex(a,0,bottom));
    }
    // Shards: palette-colored pixels of pale glass in the light where they
    // are, each catching a glint for a tic now and then (often while they
    // tumble, rarely once they lie still).
    for(const Shard &s:shards) {
        float f=renderFraction,x=s.px+(s.x-s.px)*f,y=s.py+(s.y-s.py)*f,z=s.pz+(s.z-s.pz)*f;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float light=camera.effects[2]!=0?1:lighting(sector);
        bool glint=s.landed?hash32(s.seed+(uint32_t)leveltime/5)%29==0:hash32(s.seed+(uint32_t)leveltime/2)%4==0;
        static const std::array<float,3> glass={0.58f,0.72f,0.74f};
        std::array<float,3> color;
        for(int c=0;c<3;++c)color[c]=glint?std::min(1.0f,0.35f+light):glass[c]*light;
        float size=leveltime>s.die-3*TICRATE?s.size*0.5f:s.size;
        pixelBlock(particleVertices,camera,x,y,z,size,s.landed?0.5f:s.tall,color,s.landed,&glass);
    }
}
void glassShutdown() {panes.clear();paneOfLine.clear();nearLines.clear();shards.clear();missileWatch.clear();glassVertices.clear();glassLevelSerial=~0u;}
} // namespace doom3d
