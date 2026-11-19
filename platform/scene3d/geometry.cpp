/* The visible world each frame: leaf visibility, walls, flats and things
   batched per texture. */
#include "internal.h"

namespace doom3d {
void quad(std::vector<Vertex> &out,Vertex a,Vertex b,Vertex c,Vertex d) {
    out.insert(out.end(),{a,b,c,a,c,d});
}
// Floors built alike above and below (Game_Storey: the library's): what
// repeats every storey is drawn only within its band, a storey from the slab
// under it, and the band is copied up and down (Vertex lift), as far as the
// dark lets the eye see. Per sector: the band's bottom, or NAN where none.
// Other storeys show only through the sectors taller than the band (the
// chasm): copied is what lies across one of them from the eye, or all of
// it with the eye inside one; the rest stays in its own band.
constexpr int storeyCopies=8;
static std::vector<float> storeyBottoms;
static std::vector<std::array<float,4>> tallBoxes;   // x0,y0,x1,y1 of each tall sector
static float storeyHeight;
static unsigned storeysLevel;
static bool eyeInTall;
static float storeyEye[2];
static int storeysAbove;   // copies upward: no more than the floors above his (Game_FloorsAbove)
static int storeysBelow;   // and downward (Game_FloorsBelow: none under the library's last floor)
// What doesn't repeat (the stairwells, which climb along their flight) is
// cut to the band too while the eye is outside it and looks at it across a
// tall sector (the chasm): there, past the band's walls, it would stand in
// the fog where nothing is (bandLow, the band's bottom). Seen through its
// own door it stays whole: the floor's ceiling hides the rest of it. Its
// band reaches up through the slab over the floor's ceiling (bandSlab, as
// deep as the one under its floor), as far as a door shows from across.
static bool othersBanded;
static float bandLow,bandSlab;
static bool stacked(const sector_t *sector) {return storeyHeight>0&&!std::isnan(storeyBottoms[sector-sectors]);}
static void findStoreys(const Uniforms &camera) {
    if(storeysLevel!=r_levelserial||storeyBottoms.size()!=(size_t)numsectors) {
        storeysLevel=r_levelserial;storeyHeight=0;storeyBottoms.assign(numsectors,NAN);tallBoxes.clear();
        for(int i=0;i<numsectors;++i) {float height,bottom;if(Game_Storey(i,&height,&bottom)) {storeyBottoms[i]=bottom;storeyHeight=height;bandLow=bottom;}}
        std::vector<std::array<float,4>> boxes(numsectors,{1e9f,1e9f,-1e9f,-1e9f});
        for(int i=0;i<numlines;++i)for(const sector_t *s:{lines[i].frontsector,lines[i].backsector}) {
            if(!s)continue;
            auto &b=boxes[s-sectors];
            for(const vertex_t *v:{lines[i].v1,lines[i].v2}) {
                b[0]=std::min(b[0],units(v->x));b[1]=std::min(b[1],units(v->y));b[2]=std::max(b[2],units(v->x));b[3]=std::max(b[3],units(v->y));
            }
        }
        bandSlab=storeyHeight;
        for(int i=0;i<numsectors;++i) {
            if(!stacked(&sectors[i]))continue;
            float low=storeyBottoms[i];
            if(floorZ(&sectors[i])<low-0.5f||ceilZ(&sectors[i])>low+storeyHeight+0.5f)tallBoxes.push_back(boxes[i]);
            else bandSlab=std::min(bandSlab,floorZ(&sectors[i])-low);
        }
    }
    storeyEye[0]=camera.eye[0];storeyEye[1]=camera.eye[1];eyeInTall=false;
    storeysAbove=std::min(storeyCopies,Game_FloorsAbove());
    storeysBelow=std::min(storeyCopies,Game_FloorsBelow());
    for(const auto &b:tallBoxes)eyeInTall|=storeyEye[0]>=b[0]&&storeyEye[0]<=b[2]&&storeyEye[1]>=b[1]&&storeyEye[1]<=b[3];
    othersBanded=storeyHeight>0&&stacked(R_PointInSubsector((fixed_t)(camera.eye[0]*FRACUNIT),(fixed_t)(camera.eye[1]*FRACUNIT))->sector);
}
// Whether the line from the eye to (x,y) crosses a tall sector's box.
static bool acrossTall(float x,float y) {
    if(eyeInTall)return true;
    float dx=x-storeyEye[0],dy=y-storeyEye[1];
    for(const auto &b:tallBoxes) {
        float t0=0,t1=1;
        auto slab=[&](float from,float d,float low,float high) {
            if(std::abs(d)<1e-6f)return from>=low&&from<=high;
            float a=(low-from)/d,c=(high-from)/d;if(a>c)std::swap(a,c);
            t0=std::max(t0,a);t1=std::min(t1,c);return t0<=t1;
        };
        if(slab(storeyEye[0],dx,b[0],b[2])&&slab(storeyEye[1],dy,b[1],b[3]))return true;
    }
    return false;
}
// Which storeys' copies of what lies in the box lo-hi the view can hold:
// none past the far fog's solid end (DERPINFO farfog; else storeyReach,
// across the floor) or wholly beside the view, and of the rest only those
// between its top and bottom planes.
constexpr float storeyReach=6144;
static bool storeysSeen(const Uniforms &c,const float lo[3],const float hi[3],int &kmin,int &kmax) {
    float dx=std::max({lo[0]-c.eye[0],0.0f,c.eye[0]-hi[0]}),dy=std::max({lo[1]-c.eye[1],0.0f,c.eye[1]-hi[1]});
    float reach=c.farFog[3]>0?std::min(storeyReach,c.farFogRange[1]):storeyReach;
    if(dx*dx+dy*dy>reach*reach)return false;
    // Planes through the eye, the view on their positive side; the most of
    // the box on that side (with zs widening it by all the copies' lifts).
    auto most=[&](const float n[3],float zs) {
        float sum=0;
        for(int i=0;i<3;++i) {
            float a=(lo[i]-(i==2?zs:0)-c.eye[i])*n[i],b=(hi[i]+(i==2?zs:0)-c.eye[i])*n[i];
            sum+=std::max(a,b);
        }
        return sum;
    };
    const float *f=c.forward,*r=c.right,*u=c.up;
    float px=c.projection[0],py=c.projection[1],all=storeyCopies*storeyHeight;
    float left[3]={f[0]+px*r[0],f[1]+px*r[1],f[2]+px*r[2]},right[3]={f[0]-px*r[0],f[1]-px*r[1],f[2]-px*r[2]};
    if(most(left,all)<0||most(right,all)<0)return false;
    kmin=-storeysBelow;kmax=storeysAbove;
    for(float sign:{1.0f,-1.0f}) {
        float n[3]={f[0]-sign*py*u[0],f[1]-sign*py*u[1],f[2]-sign*py*u[2]},m=most(n,0),rise=storeyHeight*n[2];
        if(rise>1e-4f)kmin=std::max(kmin,(int)std::ceil(-m/rise));
        else if(rise<-1e-4f)kmax=std::min(kmax,(int)std::floor(-m/rise));
        else if(m<0)return false;
    }
    return kmin<=kmax;
}
// The band copied up and down: every triangle added to batches since
// before, for the storeys the view holds of it (storeysSeen).
static void copyStoreys(const std::map<int,size_t> &before,const Uniforms &camera) {
    for(auto &batch:batches) {
        auto found=before.find(batch.first);
        size_t from=found==before.end()?0:found->second,to=batch.second.size();
        if(from==to)continue;
        batch.second.reserve(to+(to-from)*4);
        for(size_t n=from;n+2<to;n+=3) {
            float lo[3],hi[3];
            for(int i=0;i<3;++i) {
                const float *p=&batch.second[n+i].x;
                for(int a=0;a<3;++a) {lo[a]=i?std::min(lo[a],p[a]):p[a];hi[a]=i?std::max(hi[a],p[a]):p[a];}
            }
            int kmin,kmax;
            if(!storeysSeen(camera,lo,hi,kmin,kmax))continue;
            for(int k=kmin;k<=kmax;++k) {
                if(k==0)continue;
                for(int i=0;i<3;++i) {Vertex v=batch.second[n+i];v.lift=k*storeyHeight;batch.second.push_back(v);}
            }
        }
    }
}
static std::map<int,size_t> batchSizes() {
    std::map<int,size_t> sizes;
    for(const auto &batch:batches)sizes[batch.first]=batch.second.size();
    return sizes;
}
// This frame's glowing tiles are last frame's (keepSurfaceLights): the walls
// add none of their own.
static bool lightsKept=false;
static void addWall(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,float light,bool facing,int span) {
    const sector_t *front=sides[line.sidenum[side]].sector;
    if(stacked(front)||(othersBanded&&acrossTall((a.x+b.x)/2,(a.y+b.y)/2))) {
        float low=stacked(front)?storeyBottoms[front-sectors]:bandLow;
        bottom=std::max(bottom,low);top=std::min(top,low+storeyHeight+(stacked(front)?0:bandSlab));
    }
    if(!lightsKept)wallLights(line,side,a,b,bottom,top,tex,anchor,span);
    if(!facing)return;
    if(tex<=0||top<=bottom) return;
    tex=texturetranslation[tex]; wallImage(tex);
    float u=units(sides[line.sidenum[side]].textureoffset),len=std::hypot(b.x-a.x,b.y-a.y);
    // Phobos: a glowing wall by a pool frozen over (its edge) keeps its paint, its glow
    // fading with the pool's (the lamp fade, vLampTint alpha); the rim frost (shore.glsl)
    // whitens it as the glow goes.
    int grey=0;float glow=1;
    bool frozen=wallImage(tex).emissionWeight>0&&frozenLookBeside(line,grey,glow);
    Image &image=wallImage(tex);
    auto &out=batches[tex];unsigned mode=sectorMode(sides[line.sidenum[side]].sector);
    if(image.leaded&&settings.emissive)mode|=MODE_STAINED_GLASS;
    size_t first=out.size();
    Vertex corners[4]={{a.x,a.y,top,u,anchor-top,light,mode},{b.x,b.y,top,u+len,anchor-top,light,mode},
                       {b.x,b.y,bottom,u+len,anchor-bottom,light,mode},{a.x,a.y,bottom,u,anchor-bottom,light,mode}};
    // Wall coordinates restart on every line, so each section seeds its own tiles.
    unsigned seed=(unsigned)(&line-lines)*8u+(unsigned)side*4u+(unsigned)span+1u;
    if(!addBrokenWall(out,line,side,span,image,a,b,bottom,top,u,anchor,light,mode,seed)) {
        for(int n=0;n<4;++n) {bakeWallCoordinates(line,side,n==1||n==2?len:0,corners[n],true);corners[n].statics=seed;}
        quad(out,corners[0],corners[1],corners[2],corners[3]);
    }
    lampGlow(out,first,sides[line.sidenum[side]].sector,image);
    if(frozen)for(size_t n=first;n<out.size();++n) {
        unsigned alpha=(unsigned)std::lround(((out[n].lampTint>>24)&255u)*glow);
        out[n].lampTint=(out[n].lampTint&0xFFFFFFu)|(alpha<<24);
    }
}
// The mirrored pass draws the world a second time, so it runs only while a
// reflective floor can be on screen. Doom's own visibility answers that: the
// BSP front to back from the eye, one-sided walls and closed doors covering
// the view angles behind them until the whole view is covered. Walls are full
// height and the sky hides what lies past it, so the map's plan is enough; a
// closed door under a sky ceiling still counts as open, since sky ceilings
// rise above their walls (skyTop). Angles run from the view direction.
struct ViewCover {
    std::vector<std::pair<float,float>> spans; // Sorted, merged.
    // A span from lo across width radians, split where it wraps past behind.
    template<class F> static bool parts(float lo,float width,F &&f) {
        float hi=lo+width;
        if(hi<=doomPi) return f(lo,hi);
        return f(lo,(float)doomPi)&&f((float)-doomPi,hi-2*(float)doomPi);
    }
    bool hidden(float lo,float width) const {
        return parts(lo,width,[&](float a,float b){
            for(const auto &s:spans) if(s.first<=a&&s.second>=b) return true;
            return false;
        });
    }
    void cover(float lo,float width) {
        parts(lo,width,[&](float a,float b){
            // Merged in place: a vector a cover allocated per solid seg each frame.
            spans.push_back({a,b});std::sort(spans.begin(),spans.end());
            size_t kept=0;
            for(size_t n=0;n<spans.size();++n)
                if(kept&&spans[n].first<=spans[kept-1].second) spans[kept-1].second=std::max(spans[kept-1].second,spans[n].second);
                else spans[kept++]=spans[n];
            spans.resize(kept);return true;
        });
    }
    bool full() const {return spans.size()==1&&spans[0].first<=-doomPi&&spans[0].second>=doomPi;}
};
// The box round each node's children (node*2+child: left, bottom, right,
// top), from the leaf polygons drawn. The WAD's own boxes hold only the segs,
// and a BSP cell's polygon can reach past them (E1M2's leaf 65, north of the
// west pillar), so pruning by them hid ceilings in plain view.
static std::vector<std::array<float,4>> nodeBoxes;
static unsigned nodeBoxSerial=~0u;
static std::array<float,4> nodeBox(unsigned node) {
    std::array<float,4> box={1e9f,1e9f,-1e9f,-1e9f};
    if(node&NF_SUBSECTOR) {
        unsigned leaf=node&~NF_SUBSECTOR;
        if(leaf<(unsigned)numsubsectors&&floors[leaf].size()>=3)box=floorBounds[leaf];
        return box;
    }
    if(node>=(unsigned)numnodes) return box;
    for(int child=0;child<2;++child) {
        const auto &b=nodeBoxes[node*2+child]=nodeBox(nodes[node].children[child]);
        box={std::min(box[0],b[0]),std::min(box[1],b[1]),std::max(box[2],b[2]),std::max(box[3],b[3])};
    }
    return box;
}
// Which of the candidate leaves the eye can see (visible[leaf] set). markLines: the eye's own
// walk, which marks the lines it sees for the automap (ML_MAPPED) as vanilla's R_AddLine
// would: a seg facing the eye with some of it in view and not behind a solid wall.
static void visibleLeaves(const Uniforms &camera,const std::vector<char> &candidate,std::vector<char> &visible,bool markLines=false) {
    if(nodeBoxSerial!=r_levelserial||nodeBoxes.size()!=2*(size_t)numnodes) {
        nodeBoxSerial=r_levelserial;nodeBoxes.assign(2*(size_t)numnodes,{});
        if(numnodes)nodeBox(unsigned(numnodes-1));
    }
    float ex=camera.eye[0],ey=camera.eye[1];
    // Looking up or down widens the view's footprint on the map: the frustum's
    // corners lean back toward the eye, and once its lower or upper edge passes
    // straight down or up the footprint is the whole circle.
    float tanH=1/camera.projection[0],tanV=1/camera.projection[1];
    float flat=std::hypot(camera.forward[0],camera.forward[1]),reach=flat-tanV*std::abs(camera.forward[2]);
    // The heading from forward: Phobos' upside-down view turns right around.
    float yaw=flat>1e-4f?std::atan2(camera.forward[1],camera.forward[0]):0;
    auto relative=[&](float x,float y){return std::remainder(std::atan2(y-ey,x-ex)-yaw,2*(float)doomPi);};
    float half=reach>0.05f?std::min((float)doomPi,std::atan2(tanH,reach)+0.05f):(float)doomPi;
    if(std::abs(camera.right[2])>0.01f)half=(float)doomPi;   // rolled (Phobos grav boots): all round
    ViewCover view;
    if(half<doomPi){view.spans={{-(float)doomPi,-half},{half,(float)doomPi}};}
    // A convex shape's angles from the eye, a little wider so a wall's end
    // never quite hides a floor running up to it; false when the eye is inside.
    auto extent=[&](const Point *points,size_t count,float &lo,float &width) {
        float base=relative(points[0].x,points[0].y),low=0,high=0;
        for(size_t i=1;i<count;++i) {
            float d=std::remainder(relative(points[i].x,points[i].y)-base,2*(float)doomPi);
            low=std::min(low,d);high=std::max(high,d);
        }
        if(high-low>=doomPi-0.01f) return false;
        lo=std::remainder(base+low-0.01f,2*(float)doomPi);width=high-low+0.02f;return true;
    };
    auto seen=[&](const Point *points,size_t count) {
        float lo,width;
        return !extent(points,count,lo,width)||!view.hidden(lo,width);
    };
    std::function<void(unsigned)> walk=[&](unsigned node) {
        if(view.full()) return;
        if(node&NF_SUBSECTOR) {
            unsigned leaf=node&~NF_SUBSECTOR;
            if(leaf>=(unsigned)numsubsectors) return;
            const auto &poly=floors[leaf];
            if(candidate[leaf]&&poly.size()>=3&&seen(poly.data(),poly.size())) visible[leaf]=1;
            const subsector_t &sub=subsectors[leaf];
            for(int i=0;i<sub.numlines;++i) {
                const seg_t &seg=segs[sub.firstline+i];
                const sector_t *front=seg.frontsector,*back=seg.backsector;
                bool solid=!back||(front->ceilingpic!=skyflatnum&&back->ceilingpic!=skyflatnum&&
                    (ceilZ(back)<=floorZ(front)||floorZ(back)>=ceilZ(front)));
                if(!solid&&!markLines) continue;
                float a1=relative(units(seg.v1->x),units(seg.v1->y)),a2=relative(units(seg.v2->x),units(seg.v2->y));
                float span=std::remainder(a1-a2,2*(float)doomPi);
                if(span<=0) continue;   // facing away
                if(markLines&&!view.hidden(a2,span)) seg.linedef->flags|=ML_MAPPED;
                if(solid) view.cover(a2,span);
            }
            return;
        }
        if(node>=(unsigned)numnodes) return;
        const node_t &n=nodes[node];
        float px=ex-units(n.x),py=ey-units(n.y);
        int side=units(n.dy)*px-units(n.dx)*py>0?0:1;
        walk(n.children[side]);
        // The far side only if some of its box is still in view.
        const auto &box=nodeBoxes[node*2+(side^1)];
        if(box[0]>box[2]) return;   // no polygons past it
        Point corners[4]={{box[0],box[3]},{box[2],box[3]},{box[2],box[1]},{box[0],box[1]}};
        bool inside=ex>=box[0]&&ex<=box[2]&&ey>=box[1]&&ey<=box[3];
        if(inside||seen(corners,4)) walk(n.children[side^1]);
    };
    walk(numnodes?unsigned(numnodes-1):unsigned(NF_SUBSECTOR));
}
// Performance: Skip hidden walls and floors. The leaves the eye can see
// (visibleLeaves) and the wall sides along them; empty draws everything.
// The liquid mirror shares the eye's plan, so its view needs nothing more.
// The glass mirror keeps every leaf on the eye's side of last frame's pane:
// a new mirror fades in, so the frame it lags behind never shows. Probe
// captures (allSides) look all round and keep the whole map, as does the
// first frame after a level's load, so the vertex upload is sized for the
// whole map before play (gpu3d/draw.cpp). Things are culled by what the eye
// sees either way (thingLeaves): the probes draw no sprites. Lamps along
// hidden walls still register (wallLights runs before the facing test).
static std::vector<char> shownLeaves,shownSides,thingLeaves;
// Whether a leaf is among those the eye can see this frame (all of them while nothing is
// culled: the switch off, a probe looking all round). For what is drawn after the world
// without its depth: mist and sunbeams behind a wall still ran their shader over it.
bool leafInView(int leaf) {
    return thingLeaves.size()!=(size_t)numsubsectors||leaf<0||leaf>=numsubsectors||thingLeaves[leaf];
}
// The leaves the eye sees this frame, from one walk of the map (eyeWalk in geometry()): what
// cullHidden starts from and where findReflection looks for its floors. Empty without culling.
static std::vector<char> eyeLeaves;
static void cullHidden(const Uniforms &camera,bool allSides) {
    shownLeaves.clear();shownSides.clear();thingLeaves.clear();
    if(!settings.skipHiddenGeometry)return;
    shownLeaves=eyeLeaves;
    for(int leaf=0;leaf<numsubsectors;++leaf) {
        if(floors[leaf].size()<3)shownLeaves[leaf]=1;   // no polygon to test: its walls stay
        else if(glassMirrorActive&&!shownLeaves[leaf]) {
            // What the mirror can show: in front of its plane, and behind its glass as the
            // mirrored eye sees it. Each point is taken to where the line from the mirrored
            // eye to it crosses the plane; the leaf stays if those crossings reach the stretch
            // that is glass (windows.cpp's glassMirrorSpan), a sprite's width to spare. Without
            // the second test everything on the eye's side of the plane was kept, half the map:
            // by an E1M2 window 3,838 triangles and 177 things for 1,065 and 20 without it.
            const float *n=glassMirrorPlane;
            float side=n[0]*camera.eye[0]+n[1]*camera.eye[1]-n[3];
            float mx=camera.eye[0]-2*side*n[0],my=camera.eye[1]-2*side*n[1];
            float from=1e9f,to=-1e9f;bool any=false;
            for(Point p:floors[leaf]) {
                float front=n[0]*p.x+n[1]*p.y-n[3];
                if(front<=-1)continue;
                any=true;
                float t=side>0?side/(side+std::max(front,0.0f)):1;
                float cx=mx+(p.x-mx)*t,cy=my+(p.y-my)*t,s=-cx*n[1]+cy*n[0];
                from=std::min(from,s);to=std::max(to,s);
            }
            constexpr float spare=64;
            if(any&&to>=glassMirrorSpan[0]-spare&&from<=glassMirrorSpan[1]+spare)shownLeaves[leaf]=1;
        }
    }
    thingLeaves=shownLeaves;
    if(allSides) {shownLeaves.clear();return;}
    shownSides.assign(2*(size_t)numlines,0);
    for(int leaf=0;leaf<numsubsectors;++leaf) {
        if(!shownLeaves[leaf])continue;
        const subsector_t &sub=subsectors[leaf];
        for(int i=0;i<sub.numlines;++i) {
            const seg_t &seg=segs[sub.firstline+i];
            const line_t &line=*seg.linedef;size_t index=2*size_t(&line-lines);
            for(int side=0;side<2;++side)
                if(line.sidenum[side]>=0&&seg.sidedef==&sides[line.sidenum[side]])shownSides[index+side]=1;
        }
    }
}
// The automap draws the lines the eye has seen (ML_MAPPED), which vanilla
// marks as it draws a wall (R_StoreWallRange); this view never runs that.
// The eye's walk marks them (visibleLeaves' markLines). Without culling there
// is no such walk, so one of its own, once a tic. Marking whole leaves seen
// instead showed walls behind the eye and off to the side: too much map.
static void markSeenLines(const Uniforms &camera) {
    if(eyeLeaves.size()==(size_t)numsubsectors)return;   // the eye's walk marked them
    static int walked=-1;static unsigned level=0;
    if(walked==leveltime&&level==r_levelserial)return;
    walked=leveltime;level=r_levelserial;
    static std::vector<char> own,candidate;
    candidate.assign(numsubsectors,1);own.assign(numsubsectors,0);
    visibleLeaves(camera,candidate,own,true);
}
// The nearest reflective liquid below the eye that it can see sets this
// frame's mirror plane (visibleLeaves).
static void findReflection(const Uniforms &camera) {
    reflectionActive=false;
    if(!settings.reflections||(profile.skip&I_RENDER3D_SKIP_REFLECTIONS)||camera.effects[2]!=0)return;
    static std::vector<char> candidate,visible;
    std::vector<std::pair<float,int>> near;
    for(int i=0;i<numsectors;++i) {
        if(!sectorMist[i].reflective||sectors[i].floorpic==skyflatnum)continue;
        float z=floorZ(&sectors[i]);if(camera.eye[2]<z+1)continue;
        for(int leaf:sectorFloors[i]) {
            const auto &b=floorBounds[leaf];
            float dx=std::max({b[0]-camera.eye[0],0.0f,camera.eye[0]-b[2]}),dy=std::max({b[1]-camera.eye[1],0.0f,camera.eye[1]-b[3]});
            float d=std::hypot(dx,dy);
            if(d<1024)near.push_back({d,leaf});
        }
    }
    if(!near.empty()) {
        // The eye's walk when culling made one: the walk does not depend on which leaves it
        // records, so the floors it found are the ones a walk for them alone would find.
        if(eyeLeaves.size()==(size_t)numsubsectors)visible=eyeLeaves;
        else {
            candidate.assign(numsubsectors,0);visible.assign(numsubsectors,0);
            for(const auto &n:near)candidate[n.second]=1;
            visibleLeaves(camera,candidate,visible);
        }
        float nearest=1024;
        for(const auto &[d,leaf]:near) if(visible[leaf]&&d<nearest) {
            nearest=d;reflectionPlane=floorZ(subsectors[leaf].sector);reflectionActive=true;
        }
    }
}
// Vanilla draws sky wherever a sky flat would be, hiding everything past
// it; sky ceilings and the open band above a lower sky neighbor therefore
// go into depth with the sky shader. A sky ceiling rises to the tallest
// sprite in its sector, with sky walls along its edges up to that height,
// so trees and monsters still stand out against a low sky.
// Vanilla opens a line between two sky sectors up to the higher sky (its
// sky hack): from a low strip of sky (E1M1's along the yard's south side,
// 24 high) the yard beyond shows to the top of its walls. A low sky ceiling
// here would slice them off along the strip, so open sky sectors joined by
// such lines share the highest sky among them. Vanilla never looks down on
// a sky either; an open sky lower than the eye would lie over its floor and
// whatever stands there like a floor of sky, so it rises over the eye.
// Closed ones (ceiling at the floor) are a strip of sky beyond a wall and
// keep their height. skyTops gives each sector's sky height, in a buffer
// kept from frame to frame.
static const std::vector<float> &skyTops(float fraction,float eye) {
    static std::vector<float> skyTop;
    static std::vector<int> group;
    skyTop.resize(numsectors);
    auto open=[](const sector_t *s){return s->ceilingpic==skyflatnum&&ceilZ(s)>floorZ(s);};
    for(int i=0;i<numsectors;++i) {
        skyTop[i]=ceilZ(&sectors[i]);
        if(sectors[i].ceilingpic!=skyflatnum)continue;
        if(open(&sectors[i]))skyTop[i]=std::max(skyTop[i],eye+16);
        for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
            if(thing->sprite<0||thing->sprite>=numsprites)continue;
            const auto &sprite=sprites[thing->sprite];int frame=thing->frame&FF_FRAMEMASK;
            if(frame>=sprite.numframes)continue;
            const auto &sf=sprite.spriteframes[frame];
            float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
            for(int r=0;r<(sf.rotate?8:1);++r)skyTop[i]=std::max(skyTop[i],z+units(spritetopoffset[sf.lump[r]])+1);
        }
    }
    group.resize(numsectors);
    for(int i=0;i<numsectors;++i)group[i]=i;
    auto root=[&](int i){while(group[i]!=i)i=group[i]=group[group[i]];return i;};
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||!open(line.frontsector)||!open(line.backsector))continue;
        int a=root(int(line.frontsector-sectors)),b=root(int(line.backsector-sectors));
        if(a!=b) {group[b]=a;skyTop[a]=std::max(skyTop[a],skyTop[b]);}
    }
    for(int i=0;i<numsectors;++i)skyTop[i]=std::max(skyTop[i],skyTop[root(i)]);
    return skyTop;
}
// Floors and ceilings, batched per flat; sky flats go into skyVertices.
// storeys: only the sectors that repeat every storey (findStoreys), and
// their flats within the band; else only the rest. copied: of those, only
// the ones the other storeys show (acrossTall), else only the others.
static void flatGeometry(const std::vector<float> &skyTop,bool storeys,bool copied=false) {
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i]; if(poly.size()<3) continue;
        if(!shownLeaves.empty()&&!shownLeaves[i]) continue;
        const sector_t *sector=subsectors[i].sector;
        if(stacked(sector)!=storeys) continue;
        // Seen across the chasm by any of it: its middle, or a corner (a leaf in the corner of a
        // landing by the chasm's end is, though the line to its middle passes beside the chasm).
        float cx=0,cy=0;for(Point p:poly){cx+=p.x;cy+=p.y;}
        bool across=storeyHeight>0&&acrossTall(cx/poly.size(),cy/poly.size());
        for(size_t n=0;storeyHeight>0&&!across&&n<poly.size();++n)across=acrossTall(poly[n].x,poly[n].y);
        if(storeys&&across!=copied) continue;
        for(int ceiling=0;ceiling<2;++ceiling) {
            if(!storeys&&othersBanded&&across) {
                float z=sectorHeight(sector,ceiling);
                if(z<bandLow-0.5f||z>bandLow+storeyHeight+bandSlab+0.5f) continue;
            }
            if(storeys) {
                float z=sectorHeight(sector,ceiling),low=storeyBottoms[sector-sectors];
                if(z<low-0.5f||z>low+storeyHeight+0.5f) continue;
            }
            int flat=ceiling?sector->ceilingpic:sector->floorpic;
            if(flat==skyflatnum) {
                float z=ceiling?skyTop[sector-sectors]:floorZ(sector);
                for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]}) skyVertices.push_back({p.x,p.y,z});
                continue;
            }
            int lump=firstflat+flattranslation[flat]; lumpImage(lump,true);
            // Phobos: a pool frozen over shows only its ice sheet, on its first frame (frozenFlat).
            PoolIce ice=ceiling?PoolIce{}:poolIce(int(sector-sectors));
            bool sheet=ice.ice>=1,still=!sheet&&ice.ice>0;   // under forming ice the liquid stops moving
            // Paint lit as a lamp (lit_paint.cpp) shows its glowing copy.
            int index=int(sector-sectors);
            Image &image=sheet?frozenFlat(firstflat+flat,0,8):still?stillFlat(firstflat+flat):sectorFlatImage(index,ceiling);
            int key=sheet?frozenFlatKey(firstflat+flat,0,8):still?stillFlatKey(firstflat+flat):sectorFlatKey(index,ceiling);
            float z=sectorHeight(sector,ceiling),light=roomPulse(sector,lighting(sector));
            // MODE_MIRROR_FLOOR: this floor samples the mirrored pass.
            unsigned mirror=reflectionActive&&!ceiling&&sectorMist[sector-sectors].reflective&&std::abs(z-reflectionPlane)<0.5f?MODE_MIRROR_FLOOR:0;

            auto &out=batches[key];size_t first=out.size();
            if(sheet)mirror=0;
            if(!addBrokenFlat(out,i,int(sector-sectors),ceiling,image,z,light,mirror|sectorMode(sector)))
                for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]})
                    out.push_back({p.x,p.y,z,p.x,-p.y,light,mirror|sectorMode(sector)});
            lampGlow(out,first,sector,image);
            // The ice sheet: how far it has formed, its glow left and how grey it has gone in
            // vTint (world.frag); while it closes, laid just over the churning liquid.
            if(ice.ice>0) {
                auto &over=sheet?out:batches[frozenFlatKey(firstflat+flat,0,8)];
                size_t from=sheet?first:over.size();
                if(!sheet)for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]})
                    over.push_back({p.x,p.y,z+0.25f,p.x,-p.y,light,sectorMode(sector)});
                for(size_t n=from;n<over.size();++n) {over[n].red=ice.ice;over[n].green=ice.glow;over[n].blue=1-ice.glow;}
            }
            // Water frozen in the cold (water_ice.cpp): its ice laid just over it, the stilled
            // copy of the flat, cut texel by texel by the field (world.frag; red 2 says so).
            else if(!ceiling&&liquidLook(flat)==LIQUID_LOOK_WATER) {
                // The field at this piece's corners and middle: a strip narrower than a map cell
                // owns no cell of its own and stood open in the middle of the ice.
                float most=0,mx=0,my=0;
                for(Point p:poly) {most=std::max(most,waterIceAt(p.x,p.y));mx+=p.x;my+=p.y;}
                most=std::max(most,waterIceAt(mx/poly.size(),my/poly.size()));
                if(most>=0.25f) {
                    frozenFlat(firstflat+flat,0,8);
                    auto &over=batches[frozenFlatKey(firstflat+flat,0,8)];
                    size_t from=over.size();
                    for(size_t n=1;n+1<poly.size();++n) for(Point p:{poly[0],poly[n],poly[n+1]})
                        over.push_back({p.x,p.y,z+0.25f,p.x,-p.y,light,sectorMode(sector)});
                    for(size_t n=from;n<over.size();++n) {over[n].red=2;over[n].green=1;over[n].blue=0;}
                }
            }
        }
    }
}
// An upper or lower section the map leaves without a texture ("-") opens onto
// the void past the map, a hall of mirrors in Doom's own renderer (E1M4's line
// 693, at the end of the corridor by the lift). It borrows the texture of a wall
// meeting it at either end in the same sector: that wall's same section first,
// else its one-sided wall. Self-referencing lines (deep water, invisible
// bridges) and sky ceilings keep their gaps. Indexed line*4+side*2+(lower).
static std::vector<short> fillTextures;
static unsigned fillSerial=~0u;
static int missingFill(const line_t &line,int side,bool lower) {
    if(fillSerial!=r_levelserial||fillTextures.size()!=(size_t)numlines*4) {
        fillSerial=r_levelserial;fillTextures.assign((size_t)numlines*4,0);
        std::unordered_map<const vertex_t*,std::vector<int>> meeting;
        for(int i=0;i<numlines;++i) {meeting[lines[i].v1].push_back(i);meeting[lines[i].v2].push_back(i);}
        for(int i=0;i<numlines;++i) for(int s=0;s<2&&lines[i].backsector;++s) for(int low=0;low<2;++low) {
            const sector_t *front=sides[lines[i].sidenum[s]].sector;
            if(lines[i].frontsector==lines[i].backsector)continue;
            int same=0,solid=0;
            for(const vertex_t *v:{lines[i].v1,lines[i].v2}) for(int n:meeting[v]) for(int t=0;t<2&&n!=i;++t) {
                if(lines[n].sidenum[t]<0)continue;
                const side_t &other=sides[lines[n].sidenum[t]];
                if(other.sector!=front)continue;
                if(!lines[n].backsector) {if(!solid)solid=other.midtexture;}
                else if(!same)same=low?other.bottomtexture:other.toptexture;
            }
            fillTextures[(size_t)i*4+s*2+low]=(short)(same?same:solid);
        }
    }
    return fillTextures[(size_t)(&line-lines)*4+side*2+(lower?1:0)];
}
int wallFillTexture(const line_t &line,int side,bool lower) {return missingFill(line,side,lower);}
// The upper, lower, middle and one-sided wall sections of every line side,
// and sky walls along the edges of sky ceilings.
// allSides: also wall sides facing away from the eye, for frames that
// capture glass probes from elsewhere. storeys: as for flatGeometry, by the
// side's own sector (addWall keeps its sections within the band).
static void wallGeometry(const Uniforms &camera,const std::vector<float> &skyTop,bool allSides,bool storeys,bool copied=false) {
    auto skyWall=[](Point a,Point b,float bottom,float top) {
        if(top<=bottom)return;
        for(const Vertex &v:{Vertex{a.x,a.y,bottom},Vertex{b.x,b.y,bottom},Vertex{b.x,b.y,top},
                             Vertex{a.x,a.y,bottom},Vertex{b.x,b.y,top},Vertex{a.x,a.y,top}})skyVertices.push_back(v);
    };
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        for(int side=0;side<2;++side) {
            if(line.sidenum[side]<0) continue;
            const side_t &s=sides[line.sidenum[side]];
            const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
            if(stacked(front)!=storeys) continue;
            Point a={units((side?line.v2:line.v1)->x),units((side?line.v2:line.v1)->y)};
            Point b={units((side?line.v1:line.v2)->x),units((side?line.v1:line.v2)->y)};
            if(storeys&&(acrossTall((a.x+b.x)/2,(a.y+b.y)/2)||acrossTall(a.x,a.y)||acrossTall(b.x,b.y))!=copied) continue;
            bool shown=shownSides.empty()||shownSides[2*(size_t)i+side];
            bool facing=shown&&(allSides||(b.x-a.x)*(camera.eye[1]-a.y)-(b.y-a.y)*(camera.eye[0]-a.x)<=0);
            float floor=floorZ(front),ceil=ceilZ(front),row=units(s.rowoffset),light=roomPulse(front,lighting(front));
            // Doom's fake contrast: walls along x a step darker, walls along y a step lighter. Not with
            // whole-level lighting: there the lamps shade a wall by the way it faces, and a step with
            // a floor of 0.1 under it left the walls of a room without any light standing lit over a
            // black floor.
            if(!(settings.lightModel==1&&bakedLightsActive)) {
                if(std::abs(b.y-a.y)<0.01) light=std::max(0.1f,light-0.04f);
                if(std::abs(b.x-a.x)<0.01) light=std::min(1.0f,light+0.04f);
            }
            if(!back) {
                float anchor=ceil;
                if(s.midtexture && (line.flags&ML_DONTPEGBOTTOM)) anchor=floor+units(textureheight[s.midtexture]);
                addWall(line,side,a,b,floor,ceil,s.midtexture,anchor+row,light,facing,0);
            } else {
                float bf=floorZ(back),bc=ceilZ(back);
                bool sharedSky=front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum;
                if(bc<ceil&&!sharedSky) {
                    int tex=s.toptexture?s.toptexture:front->ceilingpic==skyflatnum?0:missingFill(line,side,false);
                    float anchor=(line.flags&ML_DONTPEGTOP)?ceil:bc+(tex?units(textureheight[tex]):0);
                    addWall(line,side,a,b,std::max(bc,floor),ceil,tex,anchor+row,light,facing,1);
                }
                if(bf>floor) {
                    float anchor=(line.flags&ML_DONTPEGBOTTOM)?ceil:bf;
                    addWall(line,side,a,b,floor,std::min(bf,ceil),s.bottomtexture?s.bottomtexture:missingFill(line,side,true),anchor+row,light,facing,2);
                }
                if(s.midtexture) {
                    int tex=texturetranslation[s.midtexture]; Image &image=wallImage(tex);
                    float low=std::max(floor,bf),high=std::min(ceil,bc);
                    float anchor=(line.flags&ML_DONTPEGBOTTOM)?low+image.height:high;
                    anchor+=row;
                    float top=std::min(high,anchor),bottom=std::max(low,anchor-image.height);
                    addWall(line,side,a,b,bottom,top,s.midtexture,anchor,light,facing,3);
                }
            }
            if(shown&&front->ceilingpic==skyflatnum)
                skyWall(a,b,back&&back->ceilingpic==skyflatnum?std::max(skyTop[back-sectors],floor):ceil,skyTop[front-sectors]);
        }
    }
}
struct Caster { const mobj_t *thing;const Image *image;int lump;bool flip;float x,y,z,distance;RagdollCard card{}; };
// The rotation of a sprite seen from (dx,dy) away, for a thing facing angle.
static unsigned spriteRotation(float dx,float dy,angle_t facing) {
    angle_t angle=(angle_t)(int64_t)(atan2(dy,dx)*(4294967296.0/(2*doomPi)));
    return (angle-facing+(unsigned)(ANG45/2)*9)>>29;
}
// F4 "Monsters block light" at work: shadows are shadow volumes (shadows.cpp's shadowVolume).
static bool volumeShadows() {
#ifdef DOOM_DEVTOOLS
    static const bool off=getenv("DERP_NOVOLUMES")!=nullptr;   // the silhouettes instead, to compare
    if(off)return false;
#endif
    return settings.shadowVolumes&&settings.lightModel==1&&bakedLightsActive;
}
// Whole-level lighting: the sun as a shadow's light, where the thing stands in it.
static bool sunShadow(float x,float y,float z,ShadowLight &light) {
    if(!(settings.lightModel==1&&bakedLightsActive&&settings.sun&&sunLevel>0)||contactCells.empty())return false;
    int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
    float visible=contactCells[4*((size_t)cy*mapWidth+cx)+2]/255.0f;
    if(visible<=0.5f)return false;
    light={x+sunAzimuth[0]*766,y+sunAzimuth[1]*766,z+643,0.6f*visible*daylightScale()};
    return true;
}
// The dynamic light on a thing (pulse, rgb) and the strongest light above it
// that can cast its shadow; returns that light's strength. The classic
// flashlight goes to beam instead, in the shaders' 1/8 steps (beamSteps).
static float dynamicThingLight(const mobj_t *thing,float x,float y,float z,std::array<float,3> &pulse,std::array<float,3> &beam,ShadowLight &shadowLight) {
    float strongest=0;
    for(unsigned n=0;n<flashes.count;++n) {
        if(fogOnly[n])continue;
        float contribution=flashAt(x,y,z+units(thing->height)*0.5f,flashes.lights[n]);
        if(settings.classicFlashlight&&flashes.lights[n].direction[3]>0) {
            float stepped=std::floor(std::min(contribution,1.0f)*8+0.5f)/8;
            for(int c=0;c<3;++c) beam[c]+=stepped*flashes.lights[n].color[c];
        } else {
            // Static lights reach sprites through the baked light at their feet.
            float dynamic=contribution*(1-(bakedLightsActive?flashes.lights[n].baked:0));
            for(int c=0;c<3;++c) pulse[c]+=dynamic*flashes.lights[n].color[c];
        }
        bool bounce=lightKinds[n]==I_RENDER3D_LIGHT_FLASHLIGHT&&flashes.lights[n].direction[3]<=0;
        if(!bounce&&lightSources[n]!=thing&&flashes.lights[n].position[2]>z+8&&contribution>strongest) {
            strongest=contribution;const Flash &f=flashes.lights[n];
            shadowLight={f.position[0],f.position[1],f.position[2],contribution};
        }
    }
    return strongest;
}
// Things standing in a liquid sink in a little, like Heretic's
// footclip (the floor hides their feet; small pickups only a
// little), and thick liquids wrap a wobbling collar of goo around
// them. Pools are often several sectors at one height, so every
// liquid floor counts, not only pits. Returns how far the sprite sinks.
static float sinkInLiquid(const mobj_t *thing,int i,const Image &image,float x,float y,const Uniforms &camera,bool collar=true,bool down=false) {
    if(!settings.splashes||(size_t)i>=sectorMist.size()||!sectorMist[i].liquid||(!down&&thing->z>sectors[i].floorheight)||
       (thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_FLOAT))||sectors[i].floorpic==skyflatnum)return 0;
    if(waterIceHolds(x,y))return 0;   // standing on water frozen over (water_ice.cpp)
    if(collar&&sectorMist[i].thick&&!(thing->flags&MF_SPECIAL)&&std::hypot(x-camera.eye[0],y-camera.eye[1])<1024) {
        float r=units(thing->radius)*0.55f;int tic=leveltime/4;
        uint32_t seed=(uint32_t)(uintptr_t)thing;
        auto color=gooColor(i,seed,x,y,floorZ(&sectors[i]),camera);
        for(int k=0;k<6;++k) {
            float angle=(k+hashUnit(k,tic,seed)*0.5f)*doomPi/3,wobble=r*(0.9f+0.25f*hashUnit(k,tic,seed^3u));
            pixelBlock(particleVertices,camera,x+std::cos(angle)*wobble,y+std::sin(angle)*wobble,floorZ(&sectors[i])+0.2f,2.0f,0.6f,color,true,&color);
        }
    }
    return std::min(8.0f,image.height*0.35f);
}
// Many WAD frames hang a few pixels lower than their foot line (top
// offset past the image height). The software renderer drew those rows
// over the floor; the depth buffer hides them, so things on solid ground
// stand on it instead: lifted by that overhang, at most 16 (larger ones
// are drawn that way on purpose). Liquids keep their sinking above.
static float floorOverhang(const mobj_t *thing,int i,const Image &image) {
    if(thing->z>thing->floorz||(thing->flags&(MF_MISSILE|MF_SPAWNCEILING|MF_NOGRAVITY))||image.top>=image.height)return 0;
    if((size_t)i<sectorMist.size()&&sectorMist[i].liquid&&!waterIceHolds(units(thing->x),units(thing->y)))return 0;
    return std::min(16.0f,(float)(image.height-image.top));
}
// The glass mirror's view (glassMirrorActive) for the sprites drawn into it:
// the mirrored eye, and the right of an ordinary camera there. The mirror
// pass's own axes are reflected, which already flips its whole image left
// to right, so sprites are laid out as that unflipped camera sees them, the
// reflected right reversed; otherwise they turn the wrong way in the glass.
struct MirrorView { float eye[2],right[2]; };
// One thing in sector i: its shadows, light, liquid footclip and sprite
// quad. Things the flashlight beam can outline join casters. With mirror,
// also its quad for the glass mirror (mirrorBatches), from the same light:
// seen from the mirrored eye, so its frame shows the side facing the glass,
// and facing the mirrored camera. The player's own sprite gets only that one.
// Phobos: a living body tumbling free (Game_Tumble): its drawing turned about its
// middle as its up turns in the view, and shortened as it leans toward or away from
// the eye. Its frames go on (walking in the air, shooting).
static void tumbleQuads(std::vector<Vertex> &v,size_t from,float x,float y,float middle,float rx,float ry,const float up[3]) {
    float side=up[0]*rx+up[1]*ry,rise=up[2],len=std::hypot(side,rise);
    if(len<1e-4f)return;
    float c=rise/len,s=-side/len,squash=std::clamp(len,0.35f,1.0f);
    for(size_t n=from;n<v.size();++n) {
        float a=(v[n].x-x)*rx+(v[n].y-y)*ry,b=(v[n].z-middle)*squash;
        float a2=a*c-b*s,b2=a*s+b*c;
        v[n].x=x+rx*a2;v[n].y=y+ry*a2;v[n].z=middle+b2;
    }
}
// Whether anything of a thing can be seen (thing_view.cpp): the thing, a body thrown from it and what
// came off that. Bodies that came to rest unseen still become sprites, one a frame: left for
// the eye to find, a room of them would all be drawn the moment it looks in.
static int unseenPictures=0,thingsDrawn=0;
// STATSDEBUG: where a frame's things spent their time (before the cull, light,
// shadows, the rest), printed when they took over a millisecond.
static uint64_t thingParts[4],thingMark;
static bool thingDebug=false;
static void thingPart(int part) {
    if(!thingDebug)return;
    uint64_t now=SDL_GetTicksNS();
    thingParts[part]+=now-thingMark;thingMark=now;
}
static bool thingWanted(const mobj_t &thing,const DeathView &death,float x,float y) {
    if(thingInView(x,y))return true;
    if(death.burst)return bodyBurstInView(*death.burst);
    if(!death.ragdoll)return false;
    if(thingInView(units(thing.x),units(thing.y))||ragdollPiecesInView(*death.ragdoll))return true;
    if(unseenPictures<=0||!ragdollWantsPicture(*death.ragdoll))return false;
    --unseenPictures;
    return true;
}
// How far from the eye a thing still shows in the glass's mirror (a monster 2,000
// units off is three pixels tall in it).
static bool mirrorNear(const Uniforms &camera,float x,float y) {
    constexpr float reach=2048;
    float dx=x-camera.eye[0],dy=y-camera.eye[1];
    return dx*dx+dy*dy<reach*reach;
}
// figureLift: someone drawn on another storey (addFigures), lifted that
// far, in no thinker: no shadows, wounds or mirror image of it.
static void addThing(mobj_t *thing,int i,const Uniforms &camera,float fraction,const Flash *beam,std::vector<Caster> &casters,
                     const MirrorView *mirror=nullptr,float figureLift=0) {
    if(thingDebug)thingMark=SDL_GetTicksNS();
    struct PartEnd { ~PartEnd() {thingPart(3);} } partEnd;
    bool figure=figureLift!=0;
    bool self=thing==players[displayplayer].mo;
    if((self&&!mirror)||thing->sprite<0||thing->sprite>=numsprites) return;
    // Furniture and pickups have no body to step, no death and no stride: out of
    // sight (and out of the mirror) they need none of what follows. A map holds
    // hundreds, and the steps below ran for each of them every frame.
    if(!figure&&!self&&thing->health>0&&!(thing->flags&(MF_COUNTKILL|MF_SHOOTABLE|MF_MISSILE|MF_CORPSE))) {
        float tx=units(thing->x),ty=units(thing->y);
        if(!thingInView(tx,ty)&&!(mirror&&mirrorNear(camera,tx,ty)))return;
    }
    // A corpse nukage rain has melted away (craters.cpp): nothing left to draw.
    if(thing->health<=0&&dissolvedAway(*thing)) return;
    // The game's airborne blood in the tint it was spilled in; none from what doesn't bleed.
    int spilled=Game_ThingRole(thing->type)==GAME_THING_BLOOD?bloodSpriteTint(thing):BLOOD_RED;
    if(spilled==BLOOD_NONE) return;
    // Gore draws a hit's mist and bits instead of the game's cartoon puff (spray.cpp, sprayViscera); the
    // game still spawns it, nothing changes in play.
    if(goreOn()&&settings.bloodSpray&&Game_ThingRole(thing->type)==GAME_THING_BLOOD) return;
    // The rocket that went off inside the balloon (caco_pop.cpp).
    if(goreOn()&&Game_ThingRole(thing->type)==GAME_THING_ROCKET&&!(thing->flags&MF_MISSILE)&&balloonHides(*thing)) {explosionCancel(*thing);return;}
    // A rocket gone off, a barrel bursting: the blast is drawn in its place (explosions.cpp).
    if(explosionHides(*thing)) return;
    // And a wall hit its own plume (wall_hits.cpp) instead of the puff, spawned as ever.
    if(settings.wallHits&&Game_ThingRole(thing->type)==GAME_THING_PUFF&&wallHitPuff(*thing)) return;
    // Varied deaths (death_director.cpp) pick a dying monster's frame, facing and mirroring.
    DeathView death={thing->sprite,thing->frame,thing->angle,false,1,0,{}};
    bool directed=!self&&settings.deathDirector&&deathView(*thing,fraction,death);
    if(!directed&&!self&&settings.deathRagdoll&&thing->health>0)directed=tumbleView(*thing,death);
    // A running monster's legs keep pace with the ground it covers (strides.cpp).
    if(!directed&&!self)strideView(*thing,death);
    // Turning shows the frames between (P_DerpDrawAngle).
    if(!directed&&!self) {unsigned drawn;if(Game_DrawAngle(thing,&drawn))death.facing=drawn;}
    // Firing as it walks: legs of their own (waist.cpp).
    // The way it walks from the way it faces, in eighths of a turn. Away from its aim by more than a quarter
    // turn it walks backwards (the user: no legs under a chest facing the other way, "the demon thing"): the
    // legs turned the other way round, at most a quarter turn off, their steps in the other order.
    unsigned legsHeading=0,legsTurn=0;int legsFrame=-1;
    if(!directed&&!self&&Game_WalkingShot(thing,&legsHeading)) {
        legsTurn=(unsigned)(death.facing-(angle_t)legsHeading+(unsigned)(ANG45/2))>>29;
        bool backwards=legsTurn>=3&&legsTurn<=5;
        if(backwards)legsTurn=(legsTurn+4)&7;
        legsFrame=strideLegs(*thing,backwards);
    }
    // Struck by a flying body (ragdoll_bumps.cpp): its pain frame for a moment, drawn only.
    if(!directed&&!self&&thing->health>0)bodyStagger(*thing,death.sprite,death.frame);
    if(death.sprite<0||death.sprite>=numsprites) return;
    const auto &sprite=sprites[death.sprite]; int frame=death.frame&FF_FRAMEMASK;
    if(frame>=sprite.numframes) return;
    const auto &sf=sprite.spriteframes[frame];
    // A procedural fall is drawn from the front view (rotation 1), like the death frames.
    auto rotationFrom=[&](const float *eye){return sf.rotate&&!death.fall.active?spriteRotation(units(thing->x)-eye[0],units(thing->y)-eye[1],death.facing):0u;};
    auto flipped=[&](unsigned seen){return (bool)sf.flip[seen]!=death.mirror;};
    unsigned rotation=rotationFrom(camera.eye);
    int lump=firstspritelump+sf.lump[rotation]; Image &image=lumpImage(lump,false);
    float x,y,z;interpolatedPosition(*thing,fraction,x,y,z);
    // A ragdoll is lit, sunk and grounded where its body lies, not where the corpse slid to: thrown
    // across a light's edge or down steps, it was lit as where it died.
    bool dollDown=false;
    if(death.ragdoll) {
        dollDown=ragdollWhere(*death.ragdoll,fraction,x,y,z);
        i=(int)(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector-sectors);
    }
    // Hit flinches and death jolts (body_motion.cpp): drawn in strips, a hop lifts the body.
    // A directed death scales the jolt by its phase and adds the landing's settle.
    BodyPose pose;bool posed=!self&&!death.fall.active&&(thing->flags&MF_COUNTKILL)&&bodyPose(*thing,fraction,pose);
    if(directed) {
        if(!posed)pose={};
        for(int k=0;k<2;++k){pose.lean[k]*=death.bend;pose.whip[k]*=death.bend;}
        pose.squash*=death.bend;pose.flatten=death.settle;
        posed=posed||death.settle>0;
    }
    if(posed)z+=pose.lift;
    // Its body has taken its step above; nothing more of it is needed where nobody looks.
    // In front of the glass that mirrors (every thing on the eye's side of its plane,
    // half the map) only what is near enough to be told apart in it: the mirror is an
    // eighth of the screen, and each thing kept is lit and shadowed in full.
    if(!figure&&!thingWanted(*thing,death,x,y)&&!(mirror&&mirrorNear(camera,x,y)))return;
    thingPart(0);
    ++thingsDrawn;
    // A monster's fullbright firing frame is lit like its others, or it stands out of the room as flat
    // paint for those frames; only the flash on it stays bright (MODE_FIRING). Always-bright monsters keep
    // Doom's look, as do dying frames and plain sprites.
    bool firing=settings.detail&&(death.frame&FF_FULLBRIGHT)&&(thing->flags&MF_COUNTKILL)&&thing->health>0&&!(states[thing->info->spawnstate].frame&FF_FULLBRIGHT);
    bool bright=(death.frame&FF_FULLBRIGHT)&&!firing;
    // Whole-level lighting: a decoration Doom draws fullbright (a lamp on its post, a torch, a
    // candle) is lit by the room like anything else, and only what glows on it stays bright
    // (MODE_LAMP_GLOW: its bright paint). Fullbright whole, the black post of a lamp stood grey
    // in a red room, apart from everything round it.
    bool lampPost=bright&&settings.detail&&settings.lightModel==1&&bakedLightsActive&&
        !(thing->flags&(MF_MISSILE|MF_SPECIAL|MF_COUNTKILL|MF_NOBLOCKMAP|MF_SHOOTABLE|MF_CORPSE));
    if(lampPost)bright=false;
    // Ragdolls cast from their current card below, after its image has been drawn.
    bool ragdoll=death.ragdoll!=nullptr||death.burst!=nullptr;   // (a burst body casts nothing either)
    // The contact shadow, the soft patch under the feet. With shadow volumes only where the thing
    // casts no volume (below): beside a volume's hard shadow it read as the old sprite shadow
    // left on (the user).
    bool volumeCast=false;
    if(!self&&!ragdoll&&!figure&&!volumeShadows())addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera);
    // What the flashlight catches throws its outline on what lies behind: a shadow volume in
    // the beam's light like any other light's, or (F4 "Monsters block light" off) the outline
    // laid on the wall behind.
    if(!self&&!ragdoll&&beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!bright) {
        if(!volumeShadows())casters.push_back({thing,&image,lump,flipped(rotation),x,y,z,std::hypot(x-camera.eye[0],y-camera.eye[1])});
        else if(!figure&&flashAt(x,y,z+units(thing->height)*0.5f,*beam)>0.02f)
            volumeCast|=shadowVolume(*thing,image,flipped(rotation),x,y,z,{beam->position[0],beam->position[1],beam->position[2],1},5);
    }
    std::array<float,3> pulse={},beamLight={};ShadowLight shadowLight={};
    float strongest=dynamicThingLight(thing,x,y,z,pulse,beamLight,shadowLight);
    // Static light at the thing's center; with bake-only lights or the
    // grid its strongest light also competes to cast the shadow.
    StaticSample statics;
    auto baked=bakedLightAt(x,y,z+units(thing->height)*0.5f,&sectors[i],&statics);
    ShadowLight fixedLight;
    // The light whose shadow volume the thing casts, if any (kind as ShadowVolumeSet's): a ragdoll casts
    // by it below, once its card is drawn.
    ShadowLight volumeLight={};int volumeKind=0;
    if(!(settings.lightModel==1&&bakedLightsActive)) {
        if(staticShadow(statics,z,fixedLight)&&fixedLight.strength>strongest) {strongest=fixedLight.strength;shadowLight=fixedLight;}
        if(!self&&!ragdoll&&!figure&&strongest>0.025f) addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera,&shadowLight);
    } else {
        // Whole-level lighting: a shadow from the strongest light on the thing. All the baked
        // light counts as one light, from the way it comes on balance where the thing stands:
        // each lamp's way by its light, a glowing pool's as from over the pool (the sample's
        // cast), the sky the place sees (through a window, a door) and what the lit surfaces
        // round it send (sky, glow). So it is the one lamp under one lamp, the row's own way
        // under a row, the window by a window, away from the pool beside a pool; gliding as the
        // thing walks, not leaping from lamp to lamp.
        // It casts where that light is one-sided enough (a third of what has a way at all; lit
        // alike from all round, nothing casts), and then as all the light there: what has
        // bounced came from the same lamps, the same window. The surfaces lose their share of
        // the lamp light and of the bounce by the bake's own balance (surface_light.glsl).
        // Beside it a shot's flash and the sun.
        // With F4 "Monsters block light" the shadow is a shadow volume (shadows.cpp's shadowVolume):
        // the light itself kept off what lies behind. Then only what has a place casts: a flash, a
        // lamp, the sun; the rest of the light, which has none, leaves the contact shadow alone.
        // Off: the outline laid over the floor and the wall.
        thingPart(1);
        struct Cast { ShadowLight light; int kind; };   // kind as ShadowVolumeSet's
        Cast cast[5];int count=0;
        bool volumes=volumeShadows();
        // (The flashlight's own volume is cast above, beside this one: the strongest light and the flashlight.)
        bool torch=volumes&&beam&&shadowLight.x==beam->position[0]&&shadowLight.y==beam->position[1]&&shadowLight.z==beam->position[2];
        if(strongest>0.025f&&!torch)cast[count++]={shadowLight,1};
        float middle=z+units(thing->height)*0.5f;
        std::array<float,3> way=statics.cast;
        float parts=statics.amount;
        float view=std::sqrt(statics.sky[0]*statics.sky[0]+statics.sky[1]*statics.sky[1]+statics.sky[2]*statics.sky[2]);
        if(view>0.01f&&skyLevel>0) {
            float sky=std::min(daylightScale(),view*8)*skyLevel;
            for(int c=0;c<3;++c)way[c]+=statics.sky[c]/view*sky;
            parts+=sky;
        }
        for(int c=0;c<3;++c)way[c]+=statics.glow[c];
        parts+=std::sqrt(statics.glow[0]*statics.glow[0]+statics.glow[1]*statics.glow[1]+statics.glow[2]*statics.glow[2]);
        float balance=std::sqrt(way[0]*way[0]+way[1]*way[1]+way[2]*way[2]);
        // Lamps first (the user: a soldier by a window cast away from the window in a room whose
        // lamp is the light one sees): where lamps give the thing real light, and one-sidedly,
        // the shadow is theirs alone. The windows, pools and lit surfaces count only where no
        // lamp does.
        float lamps=statics.amount-statics.pool;
        float lampBalance=std::sqrt(statics.lampWay[0]*statics.lampWay[0]+statics.lampWay[1]*statics.lampWay[1]+statics.lampWay[2]*statics.lampWay[2]);
        bool lampsFirst=lamps>0.04f&&lampBalance>=lamps*0.33f;
        if(lampsFirst) {way=statics.lampWay;balance=lampBalance;parts=lamps;}
        if(parts>0.01f&&balance>=parts*0.33f) {
            float shown=statics.group?1-statics.groupShare*(1-groupFlicker(statics.group)):1.0f;   // a blinking lamp's shadow goes with its light
            float there=luminance(baked.data())*shown*std::min(balance/parts,1.0f);
            // Where it stands. The light's way on balance has no place, so it falls as from far off
            // (drawn out from a made-up point 320 units off, a shadow grew to twice the thing's
            // size on a wall behind) and no flatter than from 35 degrees up (it ran long over the
            // floor where the light came in low; the user: too long and too big). Where one lamp
            // gives most of the lamps' light there, the shadow is that lamp's, from where the lamp
            // is (the user: we know where the lights are): over half its share the place glides
            // from the far point to the lamp's own, all of it at four fifths, so nothing leaps
            // as a thing walks from one lamp's light into the next.
            float u[3]={way[0]/balance,way[1]/balance,way[2]/balance},flat=std::hypot(u[0],u[1]);
            bool below=middle+u[2]*320<=z+8;   // (light from under the feet casts nothing: the test below)
            if(u[2]<flat*0.7f) {u[2]=flat*0.7f;float n=std::sqrt(flat*flat+u[2]*u[2]);for(float &c:u)c/=n;}
            float at[3]={x+u[0]*4000,y+u[1]*4000,below?z:middle+u[2]*4000};
            if(lampsFirst&&!below&&statics.shadow>=0&&statics.shadow<(int)bakeSources.size()) {
                const BakeLight &lamp=bakeSources[statics.shadow];
                float share=statics.shadowAmount*luminance(lamp.color)/lamps,t=std::clamp((share-0.5f)/0.3f,0.0f,1.0f);
                t=t*t*(3-2*t);
                if(lamp.z>z+8) {at[0]+=(lamp.x-at[0])*t;at[1]+=(lamp.y-at[1])*t;at[2]+=(lamp.z-at[2])*t;}
            }
            cast[count++]={{at[0],at[1],at[2],there},lampsFirst||statics.amount-statics.pool>parts*0.5f?2:4};   // (not mostly lamps' light but a pool's, a window's, a lit room's next door: a wide light, its shallower shadow)
        }
        if(sunShadow(x,y,z,fixedLight))cast[count++]={fixedLight,3};
        // One shadow, the strongest light's that stands above the feet (the user: several read as
        // shadows from nowhere, and one must start at the feet). The flashlight's silhouette is its own.
        count=(int)(std::remove_if(cast,cast+count,[&](const Cast &c){return c.light.z<=z+8;})-cast);
        std::sort(cast,cast+count,[](const Cast &a,const Cast &b){return a.light.strength>b.light.strength;});
        strongest=count?cast[0].light.strength:0;if(count)shadowLight=cast[0].light;
        if(volumes&&count&&cast[0].light.strength>0.025f) {volumeLight=cast[0].light;volumeKind=cast[0].kind;}
        if(!self&&!ragdoll&&!figure&&count&&cast[0].light.strength>0.025f) {
            const float from[2]={cast[0].light.x,cast[0].light.y};
            unsigned seen=rotationFrom(from);
            int seenLump=firstspritelump+sf.lump[seen];Image &outline=lumpImage(seenLump,false);
            if(volumes) {
                // (A corpse lies flat: its picture stood up as a volume is no shadow of it, so a low slab.)
                bool made=shadowVolume(*thing,outline,flipped(seen),x,y,z,cast[0].light,cast[0].kind,(thing->flags&MF_CORPSE)?0.5f:1.0f);
                volumeCast|=made;
#ifdef DOOM_DEVTOOLS
                if(getenv("DERP_SHADOWS")&&leveltime%8==0)fprintf(stderr,"volume thing %d kind %d strength %.3f made %d\n",(int)thing->type,cast[0].kind,cast[0].light.strength,(int)made);
#endif
            }
            else {
                addEnemyShadow(*thing,outline,seenLump,flipped(seen),x,y,z,camera,&cast[0].light);
                wallShadow(*thing,outline,seenLump,flipped(seen),x,y,z,camera,cast[0].light,castOpacity(cast[0].light,shadowLevel(x,y,&sectors[i])));
            }
        }
    }
    if(!self&&!ragdoll&&!figure&&volumeShadows()&&!volumeCast)addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera);
#ifdef DOOM_DEVTOOLS
    // DERP_SHADOWS: once a second, what each monster's shadow is cast by.
    if(getenv("DERP_SHADOWS")&&(thing->flags&MF_COUNTKILL)&&leveltime%35==0)
        fprintf(stderr,"shadow thing %d at %.0f,%.0f,%.0f sector %d: cast %.3f,%.3f,%.3f; strongest %.3f from %.0f,%.0f,%.0f; sky %.2f,%.2f,%.2f glow %.3f,%.3f,%.3f; light there %.3f\n",
            (int)thing->type,x,y,z,i,statics.cast[0],statics.cast[1],statics.cast[2],strongest,shadowLight.x-x,shadowLight.y-y,shadowLight.z-z,
            statics.sky[0],statics.sky[1],statics.sky[2],statics.glow[0],statics.glow[1],statics.glow[2],shadowLevel(x,y,&sectors[i]));
#endif
    thingPart(2);
    float light=bright?1.0f:roomPulse(&sectors[i],lighting(&sectors[i])); unsigned mode=MODE_SPRITE|(thing->flags&MF_SHADOW?MODE_FUZZ:0);
    // Per-pixel lighting from silhouette normals (flipped with a mirrored frame, below).
    if(settings.detail&&!bright)mode|=MODE_RELIT;
    // By a Flashbulb blast still burning: its edges toward the light picked out (MODE_RIM), the living, the dying
    // and every piece thrown off them (the pieces are drawn with the body's mode).
    if((mode&MODE_RELIT)&&!self&&((thing->flags&(MF_COUNTKILL|MF_CORPSE))||Game_ThingRole(thing->type)==GAME_THING_UNCOUNTED_MONSTER)&&explosionRim(x,y,z))mode|=MODE_RIM;
    if(firing)mode|=MODE_FIRING;
    if(lampPost)mode|=MODE_LAMP_GLOW;
    // Airborne blood shines like the floor blood (needs the normals of MODE_RELIT).
    if(settings.bloodShine&&Game_ThingRole(thing->type)==GAME_THING_BLOOD&&(mode&MODE_RELIT))mode|=MODE_WET_SPRITE;
    if(settings.glowingEyes&&(thing->flags&MF_COUNTKILL)&&thing->health>0)mode|=MODE_GLOWING_EYES;
    // A barrel's slime top glows as the nukage on the floor does (its mask: spriteSlime, images.cpp).
    if(settings.emissive&&Game_ThingRole(thing->type)==GAME_THING_BARREL)mode|=MODE_GLOWING_EYES;
    if(!bright)light=sunLightAt(x,y,&sectors[i],flowLightAt(x,y,&sectors[i],seamLightAt(x,y,&sectors[i],light)));
    // MODE_SOFT: effect sprites (shots, puffs, fogs) dither out at floors
    // and ceilings and draw in front of walls within half their width.
    bool soft=settings.softSprites&&((thing->flags&MF_MISSILE)||((thing->flags&MF_NOBLOCKMAP)&&(thing->flags&MF_NOGRAVITY)));
    if(soft)mode|=MODE_SOFT|sectorMode(&sectors[i]);
    if((thing->flags&MF_SPECIAL)&&!soft) {
        // Pickups: a steady floor so they read in the dark as lit things, and a glint now
        // and then (MODE_PICKUP, world/pickup_glint.glsl) seeded per item. Only frames Doom
        // drew fullbright (soulsphere, megasphere) keep a halo; a medkit doesn't glow.
        light=std::max(light,0.48f);
        unsigned seed=(unsigned)(((uintptr_t)thing>>4)*2654435761u>>24)&255u;
        mode|=MODE_PICKUP|(seed<<MODE_PICKUP_SEED_SHIFT)|(bright?MODE_PICKUP_GLOW:0u);
    }
    // Emissive WAD frames keep their original white cores and artwork;
    // their light still illuminates surrounding surfaces and other sprites.
    // Detail sprites light themselves per pixel from the light mask, so
    // their tint carries only the baked light. With bake-only lights they
    // shape it by where it comes from (statics).
    // The classic beam lifts toward BEAM_CEILING (lighting.glsl), never past it, as in world.frag.
    if(mode&MODE_RELIT)pulse=baked;
    else for(int c=0;c<3;++c) {pulse[c]+=baked[c];pulse[c]+=beamLight[c]*std::max(0.0f,1.2f-light-pulse[c]);}
    unsigned packedStatics=0;
    float towards=std::sqrt(statics.towards[0]*statics.towards[0]+statics.towards[1]*statics.towards[1]+statics.towards[2]*statics.towards[2]);
    if(bakeOnlyActive&&(mode&MODE_RELIT)&&statics.amount>0&&towards>1e-6f) {
        for(int c=0;c<3;++c)packedStatics|=(unsigned)std::lround((statics.towards[c]/towards*0.5f+0.5f)*255)<<(8*c);
        packedStatics|=(unsigned)std::lround(std::clamp(towards/statics.amount,0.0f,1.0f)*255)<<24;
    }
    if(bright) pulse={};
    unsigned tint=bright?0xFFFFFFFFu:packedLampTint(statics,light);
    // MODE_GROUNDED: grounding darkens the feet of things that rest on the floor.
    // Sprites have no sun map, so vSun carries strength and height above floor.
    float lift=units(thing->z-thing->floorz)+(posed?pose.lift:0),grounding=1-lift/32;
    bool grounded=settings.detail&&grounding>0&&!bright&&!soft&&Game_ThingRole(thing->type)!=GAME_THING_BLOOD&&
        !(thing->flags&(MF_SHADOW|MF_MISSILE|MF_SPAWNCEILING));
    float tumbleUp[3]={0,0,1};
    bool tumbling=!self&&!ragdoll&&!death.fall.active&&thing->health>0&&Game_Tumble(thing,fraction,tumbleUp);
    // Asleep in one of the library's beds: its frame without the blood (sleepingImage).
    bool asleep=Game_ThingAsleep(thing)!=0;
    int bleed=bloodTint(thing);   // wound rims in the monster's own blood
    // Down on one knee: drawn in its kneeling frames (kneel_view.cpp), which, every side drawn, are never flipped.
    int kneelSoldier=-1,kneelAs=0;
    bool kneeling=!self&&!ragdoll&&!death.fall.active&&kneelFrame(*thing,kneelSoldier,kneelAs);
    // The quad for one view: that view's frame, facing along its right.
    auto emit=[&](std::map<int,std::vector<Vertex>> &out,unsigned seen,float rx,float ry,bool collar) {
        int lump=firstspritelump+sf.lump[seen];
        int waist=0;
        if(legsFrame>=0&&legsFrame<sprite.numframes) {
            const auto &legs=sprite.spriteframes[legsFrame];
            unsigned turned=legs.rotate?(seen+legsTurn)&7:0;
            waist=waistKey(lump,flipped(seen),firstspritelump+legs.lump[turned],(bool)legs.flip[turned]);
        }
        Image *kneel=kneeling&&!waist?&kneelImage(kneelSoldier,kneelAs,seen,lump):nullptr;
        const bool flip=kneel?false:flipped(seen);
        Image &image=waist?waistImage(waist):kneel?*kneel:asleep?sleepingImage(lump):spilled!=BLOOD_RED?bloodSpriteImage(lump,spilled):lumpImage(lump,false);
        float left=-(float)image.left,right=left+image.width,top=z+image.top,bottom=top-image.height;
        // Phobos: a body swollen in vacuum (bodySwell), wider more than taller, standing on its feet.
        float swell=self?0:bodySwell(*thing),swellWide=1+0.18f*swell,swellTall=1+0.08f*swell;
        if(swell>0) {left*=swellWide;right*=swellWide;top=bottom+(top-bottom)*swellTall;}
        if(death.fall.active&&!death.fall.rig) {
            // A fall's drawing set down on the floor, lifted, pressed flat.
            float keep=1-death.fall.flatten;
            top=z+(image.top-death.fall.ground)*keep+death.fall.lift;bottom=z+(image.top-image.height-death.fall.ground)*keep+death.fall.lift;
        }
        float sink=!ragdoll?sinkInLiquid(thing,i,image,x,y,camera,collar):dollDown?sinkInLiquid(thing,i,image,x,y,camera,collar,true):0;
        if(!ragdoll&&!death.fall.active&&sink==0)sink=-floorOverhang(thing,i,image);   // negative: lifted onto the floor
        top-=sink;bottom-=sink;
        float u0=flip?image.width:0,u1=flip?0:image.width;
        unsigned m=mode|((mode&MODE_RELIT)&&flip?MODE_FLIPPED:0);
        // (Kneeling, not jolted: the jolt's picture is the standing frame's.)
        int jolted=self||ragdoll||waist||kneel?0:bodyJoltKey(*thing,lump,image);   // the struck part out of place (body_jolt.cpp)
        if(!jolted&&!self&&!ragdoll&&!waist&&!kneel)jolted=fatWobbleKey(*thing,lump,image);   // the fat ringing (fat_wobble.cpp)
        auto &vertices=out[waist?waist:kneel?kneelKey(kneelSoldier,kneelAs,seen):jolted?jolted:asleep?sleepingKey(lump):spilled!=BLOOD_RED?bloodSpriteKey(lump,spilled):-1-lump];size_t first=vertices.size();
        if(collar&&!self)woundView(thing,players[displayplayer].mo,image,flip,x,y,z-sink,top,bottom,rx,ry,posed?&pose:nullptr);
        // Wound holes (wounds.cpp): texels shot through, and their rims.
        // (A ragdoll's on the drawing its body was raised from: the card maps the mask by that drawing's texels.)
        int dollLump=ragdoll?ragdollLump(*thing):-1;
        const Image &woundImage=dollLump>=0?lumpImage(dollLump,false):image;
        // A ragdoll at rest is a sprite with its holes painted in (ragdollCardKept): its wounds aren't
        // worked out again each frame, unless the rain is eating it.
        const Ragdoll *lowerDoll=death.ragdoll?ragdollLower(*death.ragdoll):nullptr;   // (gore: its legs' doll takes the same mask)
        bool kept=death.ragdoll&&ragdollCardKept(*death.ragdoll)&&(!lowerDoll||ragdollCardKept(*lowerDoll))&&!cratered(*thing);
        std::vector<byte> holes;bool wounded=!self&&!kept&&woundMask(*thing,woundImage,dollLump>=0?false:flip,rx,ry,holes);
        if(wounded&&!ragdoll)fatWobbleMask(*thing,woundImage,holes);
        const std::vector<byte> *cut=wounded?&holes:nullptr;
        // Rime on Phobos, or nukage rain's soak (craters.cpp): never both, Phobos has no weather.
        unsigned frost=soft?0u:std::max(bodyFrost(*thing),bodySoak(*thing))<<MODE_FROST_SHIFT;
        auto finish=[&](std::vector<Vertex> &batch,size_t from) {
            for(size_t n=from;n<batch.size();++n) {
                batch[n].mode|=frost;
                batch[n].red=pulse[0];batch[n].green=pulse[1];batch[n].blue=pulse[2];batch[n].statics=packedStatics;
                batch[n].lampTint=tint;batch[n].lift=figureLift;
                if(grounded) {batch[n].mode|=MODE_GROUNDED;batch[n].sunU=grounding;batch[n].sunV=batch[n].z-z+lift;}
                if(soft)batch[n].sunU=std::clamp(image.width*0.5f,8.0f,64.0f);
            }
        };
        // Gore's pieces lit where they are, not where their corpse lies: a head rolled out of the dark into a
        // lit room stayed dark. The sector's light, the bake and the lamps' tint there; the thing's own
        // flashes kept on top where it isn't relit.
        auto litAt=[&](std::vector<Vertex> &batch,size_t from,float px,float py,float pz) {
            if(bright)return;
            sector_t *at=R_PointInSubsector((fixed_t)(px*FRACUNIT),(fixed_t)(py*FRACUNIT))->sector;
            StaticSample here;auto lit=bakedLightAt(px,py,pz,at,&here);
            float level=roomPulse(at,lighting(at,false));
            level=sunLightAt(px,py,at,flowLightAt(px,py,at,seamLightAt(px,py,at,level)));
            unsigned hereTint=packedLampTint(here,level);
            for(size_t n=from;n<batch.size();++n) {
                batch[n].light=level;batch[n].lampTint=hereTint;
                float *rgb[3]={&batch[n].red,&batch[n].green,&batch[n].blue};
                for(int c=0;c<3;++c)*rgb[c]=(mode&MODE_RELIT)?lit[c]:pulse[c]-baked[c]+lit[c];
            }
        };
        // Gore's loose pieces (heads, arms, chunks) cast their own shadows as the ragdoll's card does, from where
        // they are (the user: without, the chunks floated).
        auto pieceShadow=[&](const RagdollCard &piece) {
            auto found=images.find(piece.key);
            if(found==images.end())return;
            addEnemyShadow(*thing,found->second,-1-piece.key,false,piece.x,piece.y,0,camera,nullptr,true,&piece);
            // (With shadow volumes the pieces cast none: only what lies under them. Outlines laid on the
            // floor stood soft and long beside the living's volumes, the old shadow and the new at once.)
            if(strongest>0.025f&&!volumeShadows())addEnemyShadow(*thing,found->second,-1-piece.key,false,piece.x,piece.y,0,camera,&shadowLight,true,&piece);
        };
        // A ragdoll's shadow volume, from its card (the user: a monster lost its shadow the moment it
        // ragdolled): in the light the living thing cast by, and in the flashlight's. Standing as it
        // dies, a slab at half height once it lies, as a corpse sprite's is.
        auto dollVolume=[&](const RagdollCard &doll) {
            auto found=images.find(doll.key);
            if(!volumeShadows()||bright||found==images.end())return false;
            float ax,ay;cardAcross(camera,ax,ay);
            float centre=(doll.u0+doll.u1)*0.5f-doll.half,cx=doll.x+ax*centre,cy=doll.y+ay*centre;
            float squash=1-0.5f*doll.lying,feet=doll.top-doll.v1;
            bool made=false;
            if(volumeKind)made|=shadowVolume(*thing,found->second,false,cx,cy,feet,volumeLight,volumeKind,squash,&doll);
            if(beam&&flashAt(cx,cy,feet+(doll.v1-doll.v0)*squash*0.5f,*beam)>0.02f)
                made|=shadowVolume(*thing,found->second,false,cx,cy,feet,{beam->position[0],beam->position[1],beam->position[2],1},5,squash,&doll);
            return made;
        };
        RagdollCard card;
        if(death.burst) {
            // Gore: blown apart (body_burst.cpp), its pieces drawn instead of the thing, each facing the eye.
            std::vector<RagdollCard> pieces;bodyBurstCards(*death.burst,fraction,pieces);
            for(const RagdollCard &piece:pieces) {
                auto &batch=out[piece.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                float l=-piece.half,r=piece.half,b=piece.top-piece.size,t=piece.top,s=(float)piece.size;
                quad(batch,{piece.x+rx*l,piece.y+ry*l,t,0,0,light,cm},{piece.x+rx*r,piece.y+ry*r,t,s,0,light,cm},
                     {piece.x+rx*r,piece.y+ry*r,b,s,s,light,cm},{piece.x+rx*l,piece.y+ry*l,b,0,s,light,cm});
                finish(batch,from);litAt(batch,from,piece.x,piece.y,piece.top-piece.size/2.0f);
                if(collar&&piece.shadow)pieceShadow(piece);
            }
            return;
        }
        if(ragdoll) {
            // A ragdoll: its virtual camera's picture on an upright card (ragdoll.cpp), already mirrored.
            if(ragdollCard(*death.ragdoll,fraction,sink,camera.eye,cut,card,bleed)) {
                auto &batch=out[card.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                float l=-card.half*swellWide,r=card.half*swellWide,b=card.top-card.size,t=b+card.size*swellTall,s=(float)card.size;
                quad(batch,{card.x+rx*l,card.y+ry*l,t,0,0,light,cm},{card.x+rx*r,card.y+ry*r,t,s,0,light,cm},
                     {card.x+rx*r,card.y+ry*r,b,s,s,light,cm},{card.x+rx*l,card.y+ry*l,b,0,s,light,cm});
                finish(batch,from);
                // Shot out of the air: its first tics lit blood red, over the change from picture to doll.
                if(float flash=ragdollFlash(*death.ragdoll,fraction))
                    for(size_t i=from;i<batch.size();++i) {batch[i].red+=1.6f*flash;batch[i].green+=0.25f*flash;batch[i].blue+=0.2f*flash;}
                if(collar) {
                    const Image &shadow=images.at(card.key);
                    int shadowLump=-1-card.key; // the shadow renderer resolves images by -1-lump
                    // (The soft patch only where it casts no volume, as with the living.)
                    if(!dollVolume(card))addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,nullptr,false,&card);
                    if(strongest>0.025f&&!volumeShadows())addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,&shadowLight,false,&card);
                    if(!volumeShadows()&&beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!bright)
                        casters.push_back({thing,&shadow,shadowLump,false,card.x,card.y,z,std::hypot(card.x-camera.eye[0],card.y-camera.eye[1]),card});
                }
            }
            // Gore: its legs thrown off as a doll of their own, drawn as the body is.
            if(const Ragdoll *legsDoll=ragdollLower(*death.ragdoll)) {
                RagdollCard legsCard;
                if(ragdollCard(*legsDoll,fraction,sink,camera.eye,cut,legsCard,bleed)) {
                    auto &batch=out[legsCard.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                    float l=-legsCard.half,r=legsCard.half,b=legsCard.top-legsCard.size,t=b+legsCard.size,s=(float)legsCard.size;
                    quad(batch,{legsCard.x+rx*l,legsCard.y+ry*l,t,0,0,light,cm},{legsCard.x+rx*r,legsCard.y+ry*r,t,s,0,light,cm},
                         {legsCard.x+rx*r,legsCard.y+ry*r,b,s,s,light,cm},{legsCard.x+rx*l,legsCard.y+ry*l,b,0,s,light,cm});
                    finish(batch,from);litAt(batch,from,legsCard.x,legsCard.y,legsCard.top-legsCard.size/2.0f);
                    if(collar)dollVolume(legsCard);
                }
            }
            // Gore: a head bursting over it (head_burst.cpp), a picture of its own riding the neck.
            RagdollCard burst;
            // And a severed head flying off it (flying_piece.cpp): drawn the same way, where it is now.
            // And the legs left standing when it was cut in half (standing_legs.cpp).
            // And the arms shot off it and the chunks shot out of it (flying_piece.cpp).
            for(int piece=0,pieces=3+ragdollLooseCount(*death.ragdoll);piece<pieces;++piece) {
                bool drawn=piece==0?ragdollBurstCard(*death.ragdoll,fraction,burst):piece==1?ragdollHeadCard(*death.ragdoll,fraction,burst):
                           piece>=3?ragdollLooseCard(*death.ragdoll,piece-3,fraction,burst):
                           ragdollLegsCard(*death.ragdoll,fraction,burst);
                if(!drawn)continue;
                auto &batch=out[burst.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                float l=-burst.half,r=burst.half,b=burst.top-burst.size,t=burst.top,s=(float)burst.size;
                quad(batch,{burst.x+rx*l,burst.y+ry*l,t,0,0,light,cm},{burst.x+rx*r,burst.y+ry*r,t,s,0,light,cm},
                     {burst.x+rx*r,burst.y+ry*r,b,s,s,light,cm},{burst.x+rx*l,burst.y+ry*l,b,0,s,light,cm});
                finish(batch,from);litAt(batch,from,burst.x,burst.y,burst.top-burst.size/2.0f);
                if(collar&&burst.shadow)pieceShadow(burst);
            }
            // Gore, cut in half: the legs are the monster's own death frames, which the game plays on the
            // thing, with all above the cut cut away (cutMask), where the game's corpse is: the blast's
            // push carries them over backward as Doom drew it.
            float legsCut;uint32_t legsSeed;
            if(ragdollLegsCut(*death.ragdoll,legsCut,legsSeed)&&thing->sprite>=0&&thing->sprite<numsprites) {
                const auto &own=sprites[thing->sprite];int ownFrame=thing->frame&FF_FRAMEMASK;
                if(ownFrame<own.numframes) {
                    const auto &osf=own.spriteframes[ownFrame];
                    unsigned ownSeen=osf.rotate?seen:0;bool ownFlip=osf.flip[ownSeen];
                    int legsLump=firstspritelump+osf.lump[ownSeen];
                    Image &legs=lumpImage(legsLump,false);
                    std::vector<byte> cutHoles;
                    bool cut=false;
                    timed(Stat::Gore,[&]{cut=cutMask(*thing,legs,ownFlip,rx,ry,legsCut,legsSeed,cutHoles);});
                    if(cut) {
                        float lx,ly,lz;interpolatedPosition(*thing,fraction,lx,ly,lz);
                        auto &batch=out[-1-legsLump];size_t from=batch.size();
                        unsigned lm=mode|((mode&MODE_RELIT)&&ownFlip?MODE_FLIPPED:0);
                        // Standing on the floor as any frame does (floorOverhang), in the corpse's own sector.
                        int legsSector=(int)(R_PointInSubsector((fixed_t)(lx*FRACUNIT),(fixed_t)(ly*FRACUNIT))->sector-sectors);
                        float lifted=lz+legs.top+floorOverhang(thing,legsSector,legs);
                        woundedQuads(batch,{0,0,0,0,0,light,lm},legs,cutHoles,lx,ly,lifted,rx,ry,ownFlip,1,bleed);
                        finish(batch,from);
                    }
                }
            }
            return;
        }
        // (Swollen: the procedural fall and the wounded drawing are widened through rx, ry; the
        // posed body takes the widened left and right.)
        if(death.fall.rig)fallQuads(vertices,{0,0,0,0,0,light,m},death.fall,x,y,z-sink,rx*swellWide,ry*swellWide,flip,cut,bleed);
        else if(posed)bodyQuads(vertices,pose,{0,0,0,0,0,light,m},x,y,z-sink,rx,ry,left,right,image.top,image.height,u0,u1,cut,bleed);
        else if(wounded)woundedQuads(vertices,{0,0,0,0,0,light,m},image,holes,x,y,top,rx*swellWide,ry*swellWide,flip,(top-bottom)/image.height,bleed);
        else quad(vertices,{x+rx*left,y+ry*left,top,u0,0,light,m},{x+rx*right,y+ry*right,top,u1,0,light,m},
            {x+rx*right,y+ry*right,bottom,u1,(float)image.height,light,m},{x+rx*left,y+ry*left,bottom,u0,(float)image.height,light,m});
        if(tumbling)tumbleQuads(vertices,first,x,y,z-sink+units(thing->height)/2,rx,ry,tumbleUp);
        finish(vertices,first);
    };
    float acrossX,acrossY;cardAcross(camera,acrossX,acrossY);
    if(!self)emit(batches,rotation,acrossX,acrossY,!figure);
    if(mirror)emit(mirrorBatches,rotationFrom(mirror->eye),mirror->right[0],mirror->right[1],false);
}
// The people of the floors above and below (Game_LibraryFigures), so every
// floor has someone: each drawn as a thing on his floor where it stands on
// its own, lit there (the floors are built alike) and lifted to its storey,
// only where the eye looks across the chasm (as the copies) and short of
// the far fog's end. They are in no thinker: a body of their own here.
static void addFigures(const Uniforms &camera,float fraction,std::vector<Caster> &casters) {
    if(storeyHeight<=0)return;
    static std::vector<game_figure> figures(1024);
    static std::vector<mobj_t> bodies(1024);
    int n=Game_LibraryFigures(figures.data(),(int)figures.size(),gameSeconds());
    float reach=camera.farFog[3]>0?std::min(storeyReach,camera.farFogRange[1]):storeyReach;
    for(int f=0;f<n;++f) {
        const game_figure &g=figures[f];
        if(g.storey<-storeysBelow||g.storey>storeysAbove||g.type<0||!acrossTall(g.x,g.y))continue;
        float lift=g.storey*storeyHeight,dx=g.x-camera.eye[0],dy=g.y-camera.eye[1];
        if(dx*dx+dy*dy+lift*lift>reach*reach)continue;
        mobj_t &body=bodies[f];body=mobj_t{};
        body.x=(fixed_t)(g.x*FRACUNIT);body.y=(fixed_t)(g.y*FRACUNIT);
        body.subsector=R_PointInSubsector(body.x,body.y);
        body.z=body.floorz=body.subsector->sector->floorheight;body.ceilingz=body.subsector->sector->ceilingheight;
        body.angle=g.angle;body.sprite=(spritenum_t)g.sprite;body.frame=g.frame;body.type=(mobjtype_t)g.type;
        body.info=&mobjinfo[g.type];body.radius=body.info->radius;body.height=body.info->height;
        body.health=body.info->spawnhealth;body.flags=body.info->flags;
        addThing(&body,(int)(body.subsector->sector-sectors),camera,fraction,nullptr,casters,nullptr,lift);
    }
}
// The player's own shadow: the player sprite as seen from the strongest
// light above (not the flashlight or the player's own shots), or from
// the sun where the player stands in it.
static void playerShadow(const Uniforms &camera,float fraction) {
    mobj_t *self=players[displayplayer].mo;
    if(!(settings.playerShadow&&camera.effects[2]==0&&self&&self->sprite>=0&&self->sprite<numsprites))return;
    float x,y,z;interpolatedPosition(*self,fraction,x,y,z);
    ShadowLight light={};float strongest=0.025f;bool found=false;
    int kind=1;   // the light's kind for a shadow volume (ShadowVolumeSet): 1 dynamic, 2 baked, 3 the sun
    for(unsigned n=0;n<flashes.count;++n) {
        const Flash &f=flashes.lights[n];
        if(fogOnly[n]||lightSources[n]==self||lightKinds[n]==I_RENDER3D_LIGHT_FLASHLIGHT||f.position[2]<=z+8)continue;
        float amount=flashAt(x,y,z+28,f);
        if(amount>strongest) {strongest=amount;light={f.position[0],f.position[1],f.position[2],amount};found=true;}
    }
    if(bakeOnlyActive||gridLightActive()) {
        StaticSample statics;ShadowLight fixedLight;
        bakedLightAt(x,y,z+28,self->subsector->sector,&statics);
        if(staticShadow(statics,z,fixedLight)&&fixedLight.strength>strongest) {strongest=fixedLight.strength;light=fixedLight;found=true;kind=2;}
    }
    if(!found&&settings.sun&&sunLevel>0&&!contactCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        float visible=contactCells[4*((size_t)cy*mapWidth+cx)+2]/255.0f;
        if(visible>0.5f) {light={x+sunAzimuth[0]*766,y+sunAzimuth[1]*766,z+643,0.6f*visible};found=true;kind=3;}
    }
    int frame=self->frame&FF_FRAMEMASK;
    if(found&&frame<sprites[self->sprite].numframes) {
        const auto &sf=sprites[self->sprite].spriteframes[frame];
        unsigned rotation=sf.rotate?spriteRotation(light.x-x,light.y-y,self->angle):0;
        int lump=firstspritelump+sf.lump[rotation];
        // Laid on the floor, with shadow volumes too: as a volume it started under the eye, filled
        // the lower view and swung with every step (the user: very distracting).
        addEnemyShadow(*self,lumpImage(lump,false),lump,sf.flip[rotation],x,y,z,camera,&light,true);
    }
}
unsigned batchGeneration=0;   // (gpu3d/draw.cpp's drawOpaque)
void geometry(const Uniforms &camera,float fraction) {
    ++batchGeneration;
    for(auto &batch:batches)batch.second.clear();
    for(auto &batch:shadowBatches)batch.second.clear();
    for(ShadowVolumeSet &set:shadowVolumes) {set.kind=0;set.vertices.clear();}
    shadowVolumeFrame();
    for(auto &batch:mirrorBatches)batch.second.clear();
    timed(Stat::Lights,[&]{lightsKept=keepSurfaceLights(storeyHeight>0);});
    if(!lightsKept)surfaceLights.clear();
    std::copy(camera.eye,camera.eye+3,surfaceEye);
    timed(Stat::Lights,[&]{collectFlashes(fraction,camera);if(!lightsKept)collectFlatLights();});
    timed(Stat::World,[&]{
        // STATSDEBUG: which part of a slow world build it was.
        static const bool debug=getenv("STATSDEBUG")!=nullptr;
        uint64_t t0=debug?SDL_GetTicksNS():0;
        // One walk of the map for what the eye sees, for the reflection and the culling both.
        eyeLeaves.clear();
        if(settings.skipHiddenGeometry) {
            static std::vector<char> every;
            every.assign(numsubsectors,1);eyeLeaves.assign(numsubsectors,0);
            visibleLeaves(camera,every,eyeLeaves,true);
        }
        findReflection(camera);
        bool allSides=planGlassCaptures(camera)||firstFrameAfterLoad();
        cullHidden(camera,allSides);
        markSeenLines(camera);
        uint64_t t1=debug?SDL_GetTicksNS():0;
        skyVertices.clear();
        const std::vector<float> &skyTop=skyTops(fraction,camera.eye[2]);
        findStoreys(camera);
        uint64_t t2=debug?SDL_GetTicksNS():0;
        flatGeometry(skyTop,false);
        uint64_t t3=debug?SDL_GetTicksNS():0;
        wallGeometry(camera,skyTop,allSides,false);
        if(debug&&SDL_GetTicksNS()-t0>4000000)
            fprintf(stderr,"WORLD tic %d: cull %.1f sky and storeys %.1f flats %.1f walls %.1f ms%s\n",leveltime,(t1-t0)/1e6,(t2-t1)/1e6,
                (t3-t2)/1e6,(SDL_GetTicksNS()-t3)/1e6,allSides?" (whole map)":"");
        if(storeyHeight>0) {
            flatGeometry(skyTop,true,false);
            wallGeometry(camera,skyTop,allSides,true,false);
            auto before=batchSizes();
            flatGeometry(skyTop,true,true);
            wallGeometry(camera,skyTop,allSides,true,true);
            copyStoreys(before,camera);
        }
    });
    timed(Stat::Lights,collectSurfaceLights);
    timed(Stat::ShotLights,[&]{buildImpacts(camera);});
    timed(Stat::Glass,[&]{buildGlass(camera);});
    std::vector<Caster> casters;
    const Flash *beam=settings.flashlightShadows&&flashlightOn&&camera.effects[2]==0&&flashes.count&&flashes.lights[0].direction[3]>0?&flashes.lights[0]:nullptr;
    uint64_t spritesStart=profileStart();
    // The glass mirror's sprites: every thing on the eye's side of its plane.
    MirrorView mirror;
    if(glassMirrorActive&&camera.effects[2]==0) {
        const float *n=glassMirrorPlane;
        float side=n[0]*camera.eye[0]+n[1]*camera.eye[1]-n[3],along=n[0]*camera.right[0]+n[1]*camera.right[1];
        mirror={{camera.eye[0]-2*side*n[0],camera.eye[1]-2*side*n[1]},{2*along*n[0]-camera.right[0],2*along*n[1]-camera.right[1]}};
    }
    bool mirrored=glassMirrorActive&&camera.effects[2]==0;
    // The map's fixtures (the lamps) where floors repeat (findStoreys) are
    // drawn last and copied up and down with them; whatever moves isn't.
    auto fixture=[](const mobj_t *thing,int i){return stacked(&sectors[i])&&!Game_Carried(thing)&&acrossTall(units(thing->x),units(thing->y));};
    timed(Stat::Sprites,[&]{
        planThingView(storeyHeight>0?std::vector<char>{}:thingLeaves);   // (the storeys' copies are seen from everywhere)
        unseenPictures=1;thingsDrawn=0;
        static const bool debugThings=getenv("STATSDEBUG")!=nullptr;
        thingDebug=debugThings;
        for(uint64_t &part:thingParts)part=0;
        for(int pass=0;pass<(storeyHeight>0?2:1);++pass) {
            std::map<int,size_t> before;
            if(pass)before=batchSizes();
            for(int i=0;i<numsectors;++i) for(mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext) {
                if(storeyHeight>0&&fixture(thing,i)!=(pass==1))continue;
                bool inFront=mirrored&&glassMirrorPlane[0]*units(thing->x)+glassMirrorPlane[1]*units(thing->y)>glassMirrorPlane[3];
                addThing(thing,i,camera,fraction,beam,casters,inFront?&mirror:nullptr);
            }
            if(pass)copyStoreys(before,camera);
        }
        if(thingDebug&&thingParts[0]+thingParts[1]+thingParts[2]+thingParts[3]>1000000)
            fprintf(stderr,"THINGS tic %d: %d drawn; before the cull %.2f, light %.2f, shadows %.2f, rest %.2f ms\n",leveltime,thingsDrawn,
                thingParts[0]/1e6,thingParts[1]/1e6,thingParts[2]/1e6,thingParts[3]/1e6);
        thingDebug=false;
        addFigures(camera,fraction,casters);
        addExplosionCards(camera,fraction);
        buildSpeech(camera,fraction);
    });
    if(statsTiming)statsCount(Stat::Sprites,thingsDrawn);
    profileAdd(profile.frame.spritesMs,spritesStart);
    std::sort(casters.begin(),casters.end(),[](const Caster &a,const Caster &b){return a.distance<b.distance;});
    for(size_t n=0;n<std::min(size_t(6),casters.size());++n) {
        const Caster &c=casters[n];
        flashlightSilhouette(*c.thing,*c.image,c.lump,c.flip,c.x,c.y,c.z,camera,*beam,c.card.key?&c.card:nullptr);
    }
    timed(Stat::Sprites,[&]{playerShadow(camera,fraction);});
    timed(Stat::Blood,[&]{buildBlood(camera);});
    buildBookAim(camera);
    timed(Stat::Liquids,[&]{buildPuddles(camera);});
    timed(Stat::Mist,[&]{buildHeat(camera);buildDust(camera);buildEmbers(camera);buildDecompression(camera);});
    // The weather's particles, counted for the FPS readout (each a quad).
    size_t particlesBefore=particleVertices.size()+glowVertices.size();
    timed(Stat::Weather,[&]{buildWeather(camera);});
    if(statsTiming)statsCount(Stat::Weather,std::max(0,(int)(particleVertices.size()+glowVertices.size())-(int)particlesBefore)/6);
    timed(Stat::Liquids,[&]{buildBubbles(camera);});
}
} // namespace doom3d
