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
static void addWall(const line_t &line,int side,Point a,Point b,float bottom,float top,int tex,float anchor,float light,bool facing,int span) {
    const sector_t *front=sides[line.sidenum[side]].sector;
    if(stacked(front)||(othersBanded&&acrossTall((a.x+b.x)/2,(a.y+b.y)/2))) {
        float low=stacked(front)?storeyBottoms[front-sectors]:bandLow;
        bottom=std::max(bottom,low);top=std::min(top,low+storeyHeight+(stacked(front)?0:bandSlab));
    }
    wallLights(line,side,a,b,bottom,top,tex,anchor,span);
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
            spans.push_back({a,b});std::sort(spans.begin(),spans.end());
            std::vector<std::pair<float,float>> merged;
            for(const auto &s:spans)
                if(!merged.empty()&&s.first<=merged.back().second) merged.back().second=std::max(merged.back().second,s.second);
                else merged.push_back(s);
            spans=std::move(merged);return true;
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
// Which of the candidate leaves the eye can see (visible[leaf] set).
static void visibleLeaves(const Uniforms &camera,const std::vector<char> &candidate,std::vector<char> &visible) {
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
                if(!solid) continue;
                float a1=relative(units(seg.v1->x),units(seg.v1->y)),a2=relative(units(seg.v2->x),units(seg.v2->y));
                float span=std::remainder(a1-a2,2*(float)doomPi);
                if(span>0) view.cover(a2,span);
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
// captures (allSides) look all round and keep everything. Lamps along
// hidden walls still register (wallLights runs before the facing test).
static std::vector<char> shownLeaves,shownSides;
static void cullHidden(const Uniforms &camera,bool allSides) {
    shownLeaves.clear();shownSides.clear();
    if(!settings.skipHiddenGeometry||allSides)return;
    std::vector<char> candidate(numsubsectors,1);
    shownLeaves.assign(numsubsectors,0);
    visibleLeaves(camera,candidate,shownLeaves);
    for(int leaf=0;leaf<numsubsectors;++leaf) {
        if(floors[leaf].size()<3)shownLeaves[leaf]=1;   // no polygon to test: its walls stay
        else if(glassMirrorActive&&!shownLeaves[leaf]) {
            const float *n=glassMirrorPlane;
            for(Point p:floors[leaf]) if(n[0]*p.x+n[1]*p.y-n[3]>-1) {shownLeaves[leaf]=1;break;}
        }
    }
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
        candidate.assign(numsubsectors,0);visible.assign(numsubsectors,0);
        for(const auto &n:near)candidate[n.second]=1;
        visibleLeaves(camera,candidate,visible);
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
// keep their height. skyTops gives each sector's sky height.
static std::vector<float> skyTops(float fraction,float eye) {
    std::vector<float> skyTop(numsectors);
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
    std::vector<int> group(numsectors);
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
            Image &image=sheet?frozenFlat(firstflat+flat,0,8):still?stillFlat(firstflat+flat):lumpImage(lump,true);
            int key=sheet?frozenFlatKey(firstflat+flat,0,8):still?stillFlatKey(firstflat+flat):-1-lump;
            float z=sectorHeight(sector,ceiling),light=lighting(sector);
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
            float floor=floorZ(front),ceil=ceilZ(front),row=units(s.rowoffset),light=lighting(front);
            if(std::abs(b.y-a.y)<0.01) light=std::max(0.1f,light-0.04f);
            if(std::abs(b.x-a.x)<0.01) light=std::min(1.0f,light+0.04f);
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
// figureLift: someone drawn on another storey (addFigures), lifted that
// far, in no thinker: no shadows, wounds or mirror image of it.
static void addThing(mobj_t *thing,int i,const Uniforms &camera,float fraction,const Flash *beam,std::vector<Caster> &casters,
                     const MirrorView *mirror=nullptr,float figureLift=0) {
    bool figure=figureLift!=0;
    bool self=thing==players[displayplayer].mo;
    if((self&&!mirror)||thing->sprite<0||thing->sprite>=numsprites) return;
    // A corpse nukage rain has melted away (craters.cpp): nothing left to draw.
    if(thing->health<=0&&dissolvedAway(*thing)) return;
    // The game's airborne blood in the tint it was spilled in; none from what doesn't bleed.
    int spilled=Game_ThingRole(thing->type)==GAME_THING_BLOOD?bloodSpriteTint(thing):BLOOD_RED;
    if(spilled==BLOOD_NONE) return;
    // Varied deaths (death_director.cpp) pick a dying monster's frame, facing and mirroring.
    DeathView death={thing->sprite,thing->frame,thing->angle,false,1,0,{}};
    bool directed=!self&&settings.deathDirector&&deathView(*thing,fraction,death);
    if(!directed&&!self&&settings.deathRagdoll&&thing->health>0)directed=tumbleView(*thing,death);
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
    // Ragdolls cast from their current card below, after its image has been drawn.
    bool ragdoll=death.ragdoll!=nullptr;
    if(!self&&!ragdoll&&!figure)addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera);
    if(!self&&!ragdoll&&beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!(death.frame&FF_FULLBRIGHT))
        casters.push_back({thing,&image,lump,flipped(rotation),x,y,z,std::hypot(x-camera.eye[0],y-camera.eye[1])});
    std::array<float,3> pulse={},beamLight={};ShadowLight shadowLight={};
    float strongest=dynamicThingLight(thing,x,y,z,pulse,beamLight,shadowLight);
    // Static light at the thing's center; with bake-only lights or the
    // grid its strongest light also competes to cast the shadow.
    StaticSample statics;
    auto baked=bakedLightAt(x,y,z+units(thing->height)*0.5f,&sectors[i],&statics);
    ShadowLight fixedLight;
    if(staticShadow(statics,z,fixedLight)&&fixedLight.strength>strongest) {strongest=fixedLight.strength;shadowLight=fixedLight;}
    if(!self&&!ragdoll&&!figure&&strongest>0.025f) addEnemyShadow(*thing,image,lump,flipped(rotation),x,y,z,camera,&shadowLight);
    float light=lighting(&sectors[i],death.frame&FF_FULLBRIGHT); unsigned mode=MODE_SPRITE|(thing->flags&MF_SHADOW?MODE_FUZZ:0);
    // Per-pixel lighting from silhouette normals (flipped with a mirrored frame, below).
    if(settings.detail&&!(death.frame&FF_FULLBRIGHT))mode|=MODE_RELIT;
    // Airborne blood shines like the floor blood (needs the normals of MODE_RELIT).
    if(settings.bloodShine&&Game_ThingRole(thing->type)==GAME_THING_BLOOD&&(mode&MODE_RELIT))mode|=MODE_WET_SPRITE;
    if(settings.glowingEyes&&(thing->flags&MF_COUNTKILL)&&thing->health>0)mode|=MODE_GLOWING_EYES;
    if(!(death.frame&FF_FULLBRIGHT))light=sunLightAt(x,y,&sectors[i],flowLightAt(x,y,&sectors[i],seamLightAt(x,y,&sectors[i],light)));
    // MODE_SOFT: effect sprites (shots, puffs, fogs) dither out at floors
    // and ceilings and draw in front of walls within half their width.
    bool soft=settings.softSprites&&((thing->flags&MF_MISSILE)||((thing->flags&MF_NOBLOCKMAP)&&(thing->flags&MF_NOGRAVITY)));
    if(soft)mode|=MODE_SOFT|sectorMode(&sectors[i]);
    if(thing->flags&MF_SPECIAL) {
        // Pickups stay readable in the dark with a soft pulse and bloom (MODE_PICKUP).
        float phase=(float)((uintptr_t)thing%997)*0.37f;
        light=std::max(light,0.5f+0.08f*std::sin(gameSeconds()*3.0f+phase));mode|=MODE_PICKUP;
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
    if(death.frame&FF_FULLBRIGHT) pulse={};
    unsigned tint=death.frame&FF_FULLBRIGHT?0xFFFFFFFFu:packedLampTint(statics,light);
    // MODE_GROUNDED: grounding darkens the feet of things that rest on the floor.
    // Sprites have no sun map, so vSun carries strength and height above floor.
    float lift=units(thing->z-thing->floorz)+(posed?pose.lift:0),grounding=1-lift/32;
    bool grounded=settings.detail&&grounding>0&&!(death.frame&FF_FULLBRIGHT)&&!soft&&Game_ThingRole(thing->type)!=GAME_THING_BLOOD&&
        !(thing->flags&(MF_SHADOW|MF_MISSILE|MF_SPAWNCEILING));
    float tumbleUp[3]={0,0,1};
    bool tumbling=!self&&!ragdoll&&!death.fall.active&&thing->health>0&&Game_Tumble(thing,fraction,tumbleUp);
    // Asleep in one of the library's beds: its frame without the blood (sleepingImage).
    bool asleep=Game_ThingAsleep(thing)!=0;
    int bleed=bloodTint(thing);   // wound rims in the monster's own blood
    // The quad for one view: that view's frame, facing along its right.
    auto emit=[&](std::map<int,std::vector<Vertex>> &out,unsigned seen,float rx,float ry,bool collar) {
        int lump=firstspritelump+sf.lump[seen];
        Image &image=asleep?sleepingImage(lump):spilled!=BLOOD_RED?bloodSpriteImage(lump,spilled):lumpImage(lump,false);
        float left=-(float)image.left,right=left+image.width,top=z+image.top,bottom=top-image.height;
        // Phobos: a body swollen in vacuum (bodySwell), wider more than taller, standing on its feet.
        float swell=self?0:bodySwell(*thing),swellWide=1+0.18f*swell,swellTall=1+0.08f*swell;
        if(swell>0) {left*=swellWide;right*=swellWide;top=bottom+(top-bottom)*swellTall;}
        if(death.fall.active&&!death.fall.rig) {
            // A fall's drawing set down on the floor, lifted, pressed flat.
            float keep=1-death.fall.flatten;
            top=z+(image.top-death.fall.ground)*keep+death.fall.lift;bottom=z+(image.top-image.height-death.fall.ground)*keep+death.fall.lift;
        }
        float sink=!ragdoll?sinkInLiquid(thing,i,image,x,y,camera,collar):dollDown?sinkInLiquid(thing,i,image,x,y,camera,collar,true):0;top-=sink;bottom-=sink;
        float u0=flipped(seen)?image.width:0,u1=flipped(seen)?0:image.width;
        unsigned m=mode|((mode&MODE_RELIT)&&flipped(seen)?MODE_FLIPPED:0);
        auto &vertices=out[asleep?sleepingKey(lump):spilled!=BLOOD_RED?bloodSpriteKey(lump,spilled):-1-lump];size_t first=vertices.size();
        if(collar&&!self)woundView(thing,players[displayplayer].mo,image,flipped(seen),x,y,z-sink,top,bottom,rx,ry,posed?&pose:nullptr);
        // Wound holes (wounds.cpp): texels shot through, and their rims.
        std::vector<byte> holes;bool wounded=!self&&woundMask(*thing,image,flipped(seen),rx,ry,holes);
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
        RagdollCard card;
        if(ragdoll) {
            // A ragdoll: its virtual camera's picture on an upright card (ragdoll.cpp), already mirrored.
            if(ragdollCard(*death.ragdoll,fraction,sink,camera.eye,cut,card,bleed)) {
                auto &batch=out[card.key];size_t from=batch.size();unsigned cm=m&~MODE_FLIPPED;
                float l=-card.half*swellWide,r=card.half*swellWide,b=card.top-card.size,t=b+card.size*swellTall,s=(float)card.size;
                quad(batch,{card.x+rx*l,card.y+ry*l,t,0,0,light,cm},{card.x+rx*r,card.y+ry*r,t,s,0,light,cm},
                     {card.x+rx*r,card.y+ry*r,b,s,s,light,cm},{card.x+rx*l,card.y+ry*l,b,0,s,light,cm});
                finish(batch,from);
                if(collar) {
                    const Image &shadow=images.at(card.key);
                    int shadowLump=-1-card.key; // the shadow renderer resolves images by -1-lump
                    addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,nullptr,false,&card);
                    if(strongest>0.025f)addEnemyShadow(*thing,shadow,shadowLump,false,card.x,card.y,z,camera,&shadowLight,false,&card);
                    if(beam&&!(thing->flags&(MF_MISSILE|MF_NOBLOCKMAP|MF_SPECIAL))&&(thing->flags&(MF_SOLID|MF_SHOOTABLE))&&!(death.frame&FF_FULLBRIGHT))
                        casters.push_back({thing,&shadow,shadowLump,false,card.x,card.y,z,std::hypot(card.x-camera.eye[0],card.y-camera.eye[1]),card});
                }
            }
            return;
        }
        // (Swollen: the procedural fall and the wounded drawing are widened through rx, ry; the
        // posed body takes the widened left and right.)
        if(death.fall.rig)fallQuads(vertices,{0,0,0,0,0,light,m},death.fall,x,y,z-sink,rx*swellWide,ry*swellWide,flipped(seen),cut,bleed);
        else if(posed)bodyQuads(vertices,pose,{0,0,0,0,0,light,m},x,y,z-sink,rx,ry,left,right,image.top,image.height,u0,u1,cut,bleed);
        else if(wounded)woundedQuads(vertices,{0,0,0,0,0,light,m},image,holes,x,y,top,rx*swellWide,ry*swellWide,flipped(seen),(top-bottom)/image.height,bleed);
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
    for(unsigned n=0;n<flashes.count;++n) {
        const Flash &f=flashes.lights[n];
        if(fogOnly[n]||lightSources[n]==self||lightKinds[n]==I_RENDER3D_LIGHT_FLASHLIGHT||f.position[2]<=z+8)continue;
        float amount=flashAt(x,y,z+28,f);
        if(amount>strongest) {strongest=amount;light={f.position[0],f.position[1],f.position[2],amount};found=true;}
    }
    if(bakeOnlyActive||gridLightActive()) {
        StaticSample statics;ShadowLight fixedLight;
        bakedLightAt(x,y,z+28,self->subsector->sector,&statics);
        if(staticShadow(statics,z,fixedLight)&&fixedLight.strength>strongest) {strongest=fixedLight.strength;light=fixedLight;found=true;}
    }
    if(!found&&settings.sun&&sunLevel>0&&!contactCells.empty()) {
        int cx=std::clamp((int)std::floor((x-mapOrigin[0])/mapCell),0,mapWidth-1),cy=std::clamp((int)std::floor((y-mapOrigin[1])/mapCell),0,mapHeight-1);
        float visible=contactCells[4*((size_t)cy*mapWidth+cx)+2]/255.0f;
        if(visible>0.5f) {light={x+sunAzimuth[0]*766,y+sunAzimuth[1]*766,z+643,0.6f*visible};found=true;}
    }
    int frame=self->frame&FF_FRAMEMASK;
    if(found&&frame<sprites[self->sprite].numframes) {
        const auto &sf=sprites[self->sprite].spriteframes[frame];
        unsigned rotation=sf.rotate?spriteRotation(light.x-x,light.y-y,self->angle):0;
        int lump=firstspritelump+sf.lump[rotation];
        addEnemyShadow(*self,lumpImage(lump,false),lump,sf.flip[rotation],x,y,z,camera,&light,true);
    }
}
void geometry(const Uniforms &camera,float fraction) {
    for(auto &batch:batches)batch.second.clear();
    for(auto &batch:shadowBatches)batch.second.clear();
    for(auto &batch:mirrorBatches)batch.second.clear();
    surfaceLights.clear();
    std::copy(camera.eye,camera.eye+3,surfaceEye);
    timed(Stat::Lights,[&]{collectFlashes(fraction,camera);collectFlatLights();});
    timed(Stat::World,[&]{
        findReflection(camera);
        bool allSides=planGlassCaptures(camera);
        cullHidden(camera,allSides);
        skyVertices.clear();
        std::vector<float> skyTop=skyTops(fraction,camera.eye[2]);
        findStoreys(camera);
        flatGeometry(skyTop,false);
        wallGeometry(camera,skyTop,allSides,false);
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
        addFigures(camera,fraction,casters);
        buildSpeech(camera,fraction);
    });
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
