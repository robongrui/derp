/* Sprite shadows: things' shadows from the sun or a strong light, and
   silhouettes the flashlight throws on the wall behind. */
#include "internal.h"

namespace doom3d {
// The strongest visible shot or projectile supplies a world-space light here.
// Contact shadows stay separate and remain after the transient shadow fades.
// Shadow volumes: what a thing keeps a light off, as closed shapes for the stencil
// (gpu3d/frame.cpp's drawShadowVolumes). The thing's picture as the light sees it, standing
// across the light's way, cut into slabs two texels high along its covered runs; each slab
// is drawn out away from the light: a box of twelve triangles, closed, so counting its faces
// tells what lies inside. The outline is the sprite's own, in two-texel steps.
// A set per light or kind of light: two dynamic lights a frame (a shot's flash, a fireball),
// the flashlight, the sun, and the baked light, all of it in one (and one more where it is a
// wide light), each thing's volume from the way its own light comes.
// kind: 1 dynamic, 2 the baked light, 3 the sun, 4 the baked light where it is wide, 5 the flashlight (ShadowVolumeSet).
ShadowVolumeSet shadowVolumes[shadowVolumeSets];
// A picture's covered runs, slab by slab: found once a picture, not from its
// pixels for every monster every frame.
namespace {
struct VolumeRun { int row,bottom,column,end; };
struct VolumeRuns { int width=0,height=0;uint64_t picture=0;std::vector<VolumeRun> runs; };
std::unordered_map<const Image*,VolumeRuns> volumeRuns;
// picture: which drawing the image holds now. A sprite's never changes (0); a ragdoll's card is
// drawn afresh into the same image with every pose (RagdollCard::picture).
const std::vector<VolumeRun> &runsOf(const Image &image,uint64_t picture) {
    VolumeRuns &kept=volumeRuns[&image];
    if(kept.width==image.width&&kept.height==image.height&&kept.picture==picture)return kept.runs;
    kept.width=image.width;kept.height=image.height;kept.picture=picture;kept.runs.clear();
    constexpr int step=2;
    for(int row=0;row<image.height;row+=step) {
        int bottom=std::min(row+step,image.height);
        auto covered=[&](int column) {
            for(int r=row;r<bottom;++r)if(image.pixels[((size_t)r*image.width+column)*2+1])return true;
            return false;
        };
        for(int column=0;column<image.width;) {
            if(!covered(column)) {++column;continue;}
            int end=column;while(end<image.width&&covered(end))++end;
            kept.runs.push_back({row,bottom,column,end});
            column=end;
        }
    }
    return kept.runs;
}
// A thing's volume in a light that has a place of its own (the baked light, the
// sun), kept while the thing, its picture and the light stay where they are: a
// monster at its post cast the same thousand and more vertices afresh each frame.
// (Kept by the thing, or by a ragdoll's card image: a body and the legs thrown off it are one thing, two casters.)
// leaves: the leaves its shadow lands on (castsInView).
struct KeptVolume { const Image *image=nullptr;bool flip=false;int kind=0;float at[3]={},lamp[3]={},squash=1;uint64_t picture=0;std::vector<Vertex> vertices;std::vector<int> leaves;unsigned frame=0; };
std::unordered_map<const void*,KeptVolume> keptVolumes;
unsigned volumeFrame=0,volumeLevel=0,volumeSettings=0;
#ifdef DOOM_DEVTOOLS
unsigned volumesBuilt=0,volumesUnseen=0;   // DERP_SHADOWS: how many casters cast this frame, and how many cast out of sight
#endif
bool anySeen(const std::vector<int> &leaves) {
    for(int leaf:leaves)if(shadowLeafSeen(leaf))return true;
    return false;
}
}
// Once a frame, before any thing casts: a new level or new settings start
// afresh (their pictures may be others at the same addresses), and what cast
// nothing for a second is let go (its thing may be gone).
void shadowVolumeFrame() {
    if(volumeLevel!=r_levelserial||volumeSettings!=settingsSerial) {
        volumeLevel=r_levelserial;volumeSettings=settingsSerial;
        keptVolumes.clear();volumeRuns.clear();
    }
#ifdef DOOM_DEVTOOLS
    static const bool report=getenv("DERP_SHADOWS")!=nullptr;
    if(report&&volumeFrame%35==0)fprintf(stderr,"volumes: %u cast, %u of them out of sight (not drawn)\n",volumesBuilt+volumesUnseen,volumesUnseen);
    volumesBuilt=volumesUnseen=0;
#endif
    if(++volumeFrame%256==0)
        for(auto it=keptVolumes.begin();it!=keptVolumes.end();)it=volumeFrame-it->second.frame>240?keptVolumes.erase(it):std::next(it);
}
// squash: a body on the floor. Its picture is of something lying, so its rows are depth over
// the ground more than height: the volume is the picture at half its height (0.5), a low slab.
// card: a ragdoll's picture (ragdoll_card.cpp) in place of a sprite: the body as the eye sees it,
// not as the light does, stood across the light's way all the same (a second picture from the
// light's side would be the doll drawn twice a pose). x,y: the body's middle; its height is the card's.
// Its holes are the body's: what was shot off it casts nothing.
bool shadowVolume(const mobj_t &thing,const Image &image,bool flip,float x,float y,float z,const ShadowLight &light,int kind,float squash,const RagdollCard *card) {
    if(card&&(card->u1<=card->u0||card->v1<=card->v0))return false;
    const void *keeper=card?(const void*)&image:(const void*)&thing;
    uint64_t picture=card?card->picture:0;
    // The picture's covered columns and how tall it stands.
    float firstColumn=card?(float)card->u0:0,wide=card?(float)(card->u1-card->u0):(float)image.width;
    float tall=card?(card->v1-card->v0)*squash:units(thing.height);
    // (In the flashlight anything solid casts, a barrel too, as its outlines did.)
    // (And the player, whose own shadow falls as the monsters' do: geometry.cpp's playerShadow.)
    if(kind!=5&&!thing.player&&!(thing.info->flags&MF_COUNTKILL)&&Game_ThingRole(thing.type)!=GAME_THING_UNCOUNTED_MONSTER)return false;
    ShadowVolumeSet *set=nullptr;
    float reach=0;
    float dx=x-light.x,dy=y-light.y,horizontal=std::hypot(dx,dy);
    if(horizontal<4)return false;
    if(kind==1||kind==5) {
        int index=-1;
        for(unsigned n=0;n<flashes.count&&index<0;++n) {
            const Flash &f=flashes.lights[n];
            if(f.position[0]==light.x&&f.position[1]==light.y&&f.position[2]==light.z)index=(int)n;
        }
        if(index<0)return false;
        // Two dynamic lights a frame cast (a shot's flash and a fireball, say), the first two asked for; the flashlight besides.
        if(kind==5)set=&shadowVolumes[5];
        else for(int n:{0,4}) {
            ShadowVolumeSet &s=shadowVolumes[n];
            if(!s.kind||s.light[0]==(float)index) {set=&s;break;}
        }
        if(!set)return false;
        set->light[0]=(float)index;
        reach=std::min(flashes.lights[index].position[3],640.0f);
    } else {
        set=&shadowVolumes[kind==3?1:kind==4?3:2];
        auto found=keptVolumes.find(keeper);
        if(found!=keptVolumes.end()) {
            const KeptVolume &k=found->second;
            if(k.image==&image&&k.flip==flip&&k.kind==kind&&k.squash==squash&&k.picture==picture&&k.at[0]==x&&k.at[1]==y&&k.at[2]==z&&
               k.lamp[0]==light.x&&k.lamp[1]==light.y&&k.lamp[2]==light.z&&!k.vertices.empty()) {
                found->second.frame=volumeFrame;
                if(!anySeen(k.leaves)) {
#ifdef DOOM_DEVTOOLS
                    ++volumesUnseen;
#endif
                    return true;
                }
#ifdef DOOM_DEVTOOLS
                ++volumesBuilt;
#endif
                set->kind=kind;
                set->vertices.insert(set->vertices.end(),k.vertices.begin(),k.vertices.end());
                return true;
            }
        }
        // As far as the first wall behind the thing: past it the lamp does not shine, and the bake's
        // sum there is other lamps' (the sun's shadow is no longer than the thing is tall, or so).
        // (The walls in its way are found below, for every kind of light.)
        // No longer than three times the thing's height along the light's way (the user: the
        // shadows stood too long): a lamp's shadow is lost in the other light a little way off.
        reach=kind==3?220.0f:std::clamp(tall*3,96.0f,400.0f);
    }
    if(image.width<=0||image.height<=0||image.pixels.size()<(size_t)image.width*image.height*2)return false;
    const float lamp[3]={light.x,light.y,light.z};
    // left: where column 0 stands across the light's way; top: row 0's height. A card's covered
    // columns stand about the body's middle, its lowest covered row on what the body lies on.
    float rx=dy/horizontal,ry=-dx/horizontal,left=card?-(card->u0+card->u1)*0.5f:-(float)image.left;
    float top=card?card->top-card->v1+card->v1*squash:z+image.top*squash;
    float middle=card?card->top-card->v1+tall*0.5f:z+tall*0.5f;
    static std::vector<Vertex> out;   // the volume, added to its set below if it lands in sight
    out.clear();
    // No shadow through a wall: how far over the ground the light's way runs behind the thing
    // before a wall stands in it; the volume ends there (and 8 units on, to reach the wall's
    // face on a slant). Rays across the picture find it, as many as keep them some 24 units
    // apart where they land: a lamp far off sees the picture head on and a few do, but the
    // flashlight an arm's length from a monster fans them out ten times over, and with three
    // rays (and then eight) a doorway or a recess in the wall behind fell between two of them:
    // the shadow ended at the wall's face and what lay deeper stood lit inside it (the user: up
    // close they are missing their inner part). Where two neighbours differ (one went on, or
    // found another wall well behind) the place between them where the wall ends is searched
    // for, three halvings, and the shadow steps there.
    struct WallEdge { float part,before,after; };   // across the picture (0 left - 1 right): the wall just left of here, and just right
    static std::vector<WallEdge> edges;
    static std::vector<int> nearLines;
    {
        // (Only the lines its rays can reach are asked: each ray otherwise went through every line of the map.)
        float span=reach+wide+16;
        nearLines.clear();
        for(int i=0;i<numlines;++i) {
            const line_t &line=lines[i];
            float ax=units(line.v1->x),ay=units(line.v1->y),bx=units(line.v2->x),by=units(line.v2->y);
            if(std::max(ax,bx)<x-span||std::min(ax,bx)>x+span||std::max(ay,by)<y-span||std::min(ay,by)>y+span)continue;
            nearLines.push_back(i);
        }
    }
    auto wallFrom=[&](float part) {
        float across=left+firstColumn+wide*part,px=x+rx*across,py=y+ry*across,ax=px-light.x,ay=py-light.y,al=std::hypot(ax,ay);
        WallHit hit;
        return al>1e-3f&&firstSolidWall(px,py,middle,ax/al,ay/al,0,reach,hit,&nearLines)?hit.t+8:1e9f;
    };
    auto apart=[](float a,float b) {return (a>1e8f)!=(b>1e8f)||(a<1e8f&&std::abs(a-b)>24);};
    int rays=std::clamp((int)std::ceil(wide*(horizontal+reach)/(horizontal*24)),4,32);
    edges.clear();
    float was=wallFrom(0);
    edges.push_back({0,was,was});
    for(int k=1;k<=rays;++k) {
        float part=(float)k/rays,now=wallFrom(part);
        if(apart(was,now)) {
            float lo=(float)(k-1)/rays,hi=part,a=was,b=now;
            for(int n=0;n<3;++n) {
                float mid=(lo+hi)*0.5f,w=wallFrom(mid);
                bool withA=(w>1e8f)==(a>1e8f)&&((b>1e8f)!=(w>1e8f)||std::abs(w-a)<=std::abs(w-b));
                if(withA) {lo=mid;a=w;} else {hi=mid;b=w;}
            }
            edges.push_back({(lo+hi)*0.5f,a,b});
        }
        edges.push_back({part,now,now});
        was=now;
    }
    // (A straight wall needs its two ends only: what lies on the line between its neighbours is left out,
    // or every run of the picture was cut into as many boxes as there were rays.)
    for(size_t n=1;n+1<edges.size();) {
        const WallEdge &l=edges[n-1],&m=edges[n],&r=edges[n+1];
        bool flat=m.before==m.after&&((l.after>1e8f&&m.before>1e8f&&r.before>1e8f)||
            (l.after<1e8f&&m.before<1e8f&&r.before<1e8f&&std::abs(m.before-(l.after+(r.before-l.after)*(m.part-l.part)/std::max(1e-6f,r.part-l.part)))<2));
        if(flat)edges.erase(edges.begin()+n);else ++n;
    }
    // The wall behind a place across the picture: straight from one edge to the next.
    auto wallBehind=[&](float part) {
        size_t n=1;
        while(n+1<edges.size()&&edges[n].part<=part)++n;
        const WallEdge &l=edges[n-1],&r=edges[n];
        if(l.after>1e8f||r.before>1e8f)return std::max(l.after,r.before);
        return l.after+(r.before-l.after)*std::clamp((part-l.part)/std::max(1e-6f,r.part-l.part),0.0f,1.0f);
    };
    // Cast by sight: the leaves the shadow lands on, along the light's way across the picture from
    // the feet to where it ends, up to the wall's face and not through it. A shadow that lands on no
    // leaf the eye sees darkens nothing on screen and is not drawn: every kind of light with a volume
    // is a round of the shadow pass over the whole screen (gpu3d/frame.cpp), and monsters in the
    // room next door, behind a wall, had the pass run for shadows no one could see.
    // (A volume kept for a fixed light keeps its leaves and is built all the same: it costs nothing
    // while unseen and is ready when the eye turns. A dynamic light's is built only if seen.)
    static std::vector<int> landed;
    landed.clear();
    const bool dynamic=kind==1||kind==5;
    bool seen=false;
    for(int k=0;k<=rays&&!(dynamic&&seen);++k) {
        float part=(float)k/rays,across=left+firstColumn+wide*part,px=x+rx*across,py=y+ry*across;
        float ax=px-light.x,ay=py-light.y,al=std::hypot(ax,ay);
        if(al<1e-3f)continue;
        float wall=wallBehind(part),run=wall<1e8f?std::min(reach,std::max(0.0f,wall-9)):reach;   // (wallBehind is 8 past the face)
        for(float t=0;;t+=12) {
            float d=std::min(t,run);
            int leaf=(int)(R_PointInSubsector((fixed_t)((px+ax/al*d)*FRACUNIT),(fixed_t)((py+ay/al*d)*FRACUNIT))-subsectors);
            if(std::find(landed.begin(),landed.end(),leaf)==landed.end()) {landed.push_back(leaf);seen=seen||shadowLeafSeen(leaf);}
            if(d>=run||(dynamic&&seen))break;
        }
    }
    if(dynamic&&!seen) {
#ifdef DOOM_DEVTOOLS
        ++volumesUnseen;
#endif
        return true;
    }
    // On other things' sprites the volume starts behind the thing's own picture (pushBy along the
    // light's way: the picture's reach to either side of its middle and a little, as it may stand
    // any way to the light), or half of its own sprite lay in its own shadow. Each near corner
    // carries that step in its tint, which world.vert adds for the sprites' round (c.shadowSkip.w;
    // gpu3d/frame.cpp) and leaves out for the world's, where the shadow starts at the feet.
    float pushBy=(card?wide*0.5f:std::max((float)image.left,(float)image.width-image.left))+4;
    // A corner of the picture, and the same drawn out from the light. inward: a hair toward the
    // middle of the piece it is a corner of, in columns: at a step in the wall the piece takes
    // the wall on its own side of it.
    auto corner=[&](float column,int row,bool far,float inward,float p[3],float push[3]) {
        float across=left+(flip?image.width-column:column);
        p[0]=x+rx*across;p[1]=y+ry*across;p[2]=top-row*squash;
        push[0]=push[1]=push[2]=0;
        float d[3]={p[0]-lamp[0],p[1]-lamp[1],p[2]-lamp[2]};
        float length=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(length<=1e-3f)return;
        float part=std::clamp((across+(flip?-inward:inward)-left-firstColumn)/std::max(1.0f,wide),0.0f,1.0f);
        float wall=wallBehind(part),flat=std::hypot(d[0],d[1]);
        float run=flat>1e-3f?std::min(reach,wall*length/flat):reach;
        if(far)for(int c=0;c<3;++c)p[c]+=d[c]/length*run;
        else for(int c=0;c<3;++c)push[c]=d[c]/length*std::min(pushBy,run*0.8f);   // (never past its far end)
    };
    size_t first=out.size();
    static std::vector<float> cuts;
    for(const VolumeRun &run:runsOf(image,picture)) {
        // A run is one box, its far end straight between its two ends: it is cut at the wall's
        // edges that fall inside it, so each piece ends at its own stretch of wall.
        cuts.clear();
        cuts.push_back((float)run.column);
        for(size_t n=1;n+1<edges.size();++n) {
            float at=firstColumn+wide*edges[n].part;
            if(flip)at=image.width-at;
            if(at>run.column+0.02f&&at<run.end-0.02f)cuts.push_back(at);
        }
        std::sort(cuts.begin(),cuts.end());
        cuts.push_back((float)run.end);
        for(size_t piece=0;piece+1<cuts.size();++piece) {
            // Left and right as the light sees them, whichever way the picture is turned: a mirrored
            // frame's boxes were wound the other way round, counted as minus one where the others
            // count one, and where the two kinds' shadows lay over each other they summed to none.
            float l=flip?cuts[piece+1]:cuts[piece],r=flip?cuts[piece]:cuts[piece+1];
            float li=flip?-0.01f:0.01f,ri=-li;
            float p[8][3],push[8][3];   // near: 0 top left, 1 top right, 2 bottom right, 3 bottom left; far: 4-7 the same
            for(int far=0;far<2;++far) {
                corner(l,run.row,far,li,p[far*4],push[far*4]);corner(r,run.row,far,ri,p[far*4+1],push[far*4+1]);
                corner(r,run.bottom,far,ri,p[far*4+2],push[far*4+2]);corner(l,run.bottom,far,li,p[far*4+3],push[far*4+3]);
            }
            static const int faces[6][4]={{0,1,2,3},{5,4,7,6},{4,5,1,0},{3,2,6,7},{4,0,3,7},{1,5,6,2}};
            for(const auto &f:faces)for(int k:{0,1,2,0,2,3}) {
                Vertex v{p[f[k]][0],p[f[k]][1],p[f[k]][2],0,0,0,0};
                v.red=push[f[k]][0];v.green=push[f[k]][1];v.blue=push[f[k]][2];
                out.push_back(v);
            }
        }
    }
    // (A dynamic light's volume moves with the light: not kept.)
    if(!dynamic) {
        KeptVolume &k=keptVolumes[keeper];
        k.image=&image;k.flip=flip;k.kind=kind;k.squash=squash;k.picture=picture;k.at[0]=x;k.at[1]=y;k.at[2]=z;
        k.lamp[0]=light.x;k.lamp[1]=light.y;k.lamp[2]=light.z;
        k.vertices.assign(out.begin()+first,out.end());k.leaves=landed;k.frame=volumeFrame;
    }
#ifdef DOOM_DEVTOOLS
    ++(seen?volumesBuilt:volumesUnseen);
#endif
    if(!seen)return true;
    set->kind=kind;
    set->vertices.insert(set->vertices.end(),out.begin()+first,out.end());
    return true;
}
float shadowLevel(float x,float y,const sector_t *sector) {
    float level=lighting(sector);
    if(!(settings.lightModel==1&&bakedLightsActive))return level;
    auto baked=bakedLightAt(x,y,floorZ(sector)+28,sector);
    return luminance(baked.data())+sunLightAt(x,y,sector,level);
}
// As dark as the light's share of what falls there, since that is what the thing keeps off the
// surface behind it: a lamp among others throws a faint shadow, the only light a deep one.
float castOpacity(const ShadowLight &light,float level) {
    return std::clamp(0.8f*light.strength/std::max({level,light.strength,0.02f}),0.0f,0.75f);
}
void addEnemyShadow(const mobj_t &thing,const Image &image,int lump,bool flip,
                    float x,float y,float z,const Uniforms &camera,const ShadowLight *light,bool anyThing,const RagdollCard *card) {
    if(!anyThing&&!(thing.info->flags&MF_COUNTKILL)&&Game_ThingRole(thing.type)!=GAME_THING_UNCOUNTED_MONSTER) return;
    float u0=0,v0=0,u1=image.width,v1=image.height;
    if(card) {
        if(card->u1<=card->u0||card->v1<=card->v0)return;
        u0=card->u0;v0=card->v0;u1=card->u1;v1=card->v1;
        float centre=(u0+u1)*0.5f-card->half;
        float ax,ay;cardAcross(camera,ax,ay);
        x+=ax*centre;y+=ay*centre;z=card->top-v1;
    }
    const sector_t *sector=card?R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector:thing.subsector->sector;
    if(sector->floorpic==skyflatnum) return;
    float floor=floorZ(sector),height=std::max(0.0f,z-floor);
    // The light a shadow is seen against: the sector's level. In whole-level lighting that is
    // only the room's fill, next to nothing, and by it every shadow was dropped; there it is
    // the light that falls on the place: the lamps, the bounce, the sun and the fill.
    bool whole=settings.lightModel==1&&bakedLightsActive;
    float level=shadowLevel(x,y,sector);
    float opacity=0.28f*std::min(level,1.0f)/(1.0f+height/48.0f);
    if(card&&card->shadow)opacity*=1.6f;   // gore's small pieces: a darker contact, or they float (the user)
    if(opacity<0.015f&&!(whole&&light)) return;   // a shot's light throws a shadow in a room with no other
    float width=(card?u1-u0:std::clamp(units(thing.radius)*2.0f,20.0f,80.0f))*std::min(1.7f,1.0f+height/160.0f);
    float length=width*0.65f,start=-length*0.35f;
    Point across;cardAcross(camera,across.x,across.y);
    Point away={-across.y,across.x};
    if(light) {
        float dx=x-light->x,dy=y-light->y,distance=std::hypot(dx,dy);
        if(distance>1.0f&&light->z>z+8.0f) {
            away={dx/distance,dy/distance};across={away.y,-away.x};
            length=std::clamp(distance*(card?v1-v0:units(thing.height))/(light->z-z),std::min(width*0.65f,192.0f),192.0f);
            // Whole-level lighting: as dark as the light's share of what falls there, since that
            // is what the thing keeps off the floor; a lamp among others throws a faint shadow,
            // the only one a deep one.
            start=0;opacity=whole?castOpacity(*light,level):std::clamp(light->strength*0.55f,0.0f,0.65f);
            // Whole-level lighting: it ends at the first wall in its way, which takes it from there
            // (wallShadow); it lay on through the wall onto the floor of the room behind.
            WallHit hit;
            if(whole&&firstSolidWall(x,y,z+8,away.x,away.y,0,length,hit))length=std::max(hit.t,1.0f);
        } else if(whole)return;   // a light too low or straight overhead: no second contact shadow
    }
    auto point=[&](float u,float v) {return Point{x+across.x*u+away.x*v,y+across.y*u+away.y*v};};
    std::vector<Point> footprint={point(-width/2,start),point(width/2,start),
                                  point(width/2,start+length),point(-width/2,start+length)};
    std::array<float,4> bounds={1e9f,1e9f,-1e9f,-1e9f};
    for(Point p:footprint) {
        bounds[0]=std::min(bounds[0],p.x);bounds[1]=std::min(bounds[1],p.y);
        bounds[2]=std::max(bounds[2],p.x);bounds[3]=std::max(bounds[3],p.y);
    }
    auto &out=shadowBatches[lump];
    // Whole-level lighting: the shadow also falls on the floor next door at the same height;
    // a sector's edge is no edge of the floor.
    std::vector<int> leaves=sectorFloors[sector-sectors];
    if(whole) {
        std::vector<const sector_t*> beside;
        for(int n=0;n<sector->linecount;++n) {
            const line_t *line=sector->lines[n];
            const sector_t *other=line->frontsector==sector?line->backsector:line->frontsector;
            if(!other||other==sector||other->floorheight!=sector->floorheight||other->floorpic==skyflatnum)continue;
            if(std::find(beside.begin(),beside.end(),other)!=beside.end())continue;
            beside.push_back(other);
            const auto &more=sectorFloors[other-sectors];leaves.insert(leaves.end(),more.begin(),more.end());
        }
    }
    for(int leaf:leaves) {
        const auto &b=floorBounds[leaf];
        if(b[2]<bounds[0]||b[0]>bounds[2]||b[3]<bounds[1]||b[1]>bounds[3]) continue;
        const auto &receiver=floors[leaf];if(receiver.size()<3) continue;
        auto polygon=receiver;
        for(size_t n=0;n<footprint.size()&&!polygon.empty();++n) {
            Point a=footprint[n],b=footprint[(n+1)%footprint.size()];
            polygon=clip(polygon,a,{b.x-a.x,b.y-a.y},false);
        }
        auto vertex=[&](Point p) {
            float dx=p.x-x,dy=p.y-y;
            float u=(dx*across.x+dy*across.y)/width+0.5f;
            float v=1.0f-((dx*away.x+dy*away.y)-start)/length;
            // Twice the blood's lift, so shadows fall on top of blood decals.
            return Vertex{p.x,p.y,floor+2*decalLift(p.x,p.y,camera),u0+(flip?1-u:u)*(u1-u0),v0+v*(v1-v0),opacity,0};
        };
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]}) out.push_back(vertex(p));
    }
}

struct WallPoint { float s,z,u,v; };
static std::vector<WallPoint> clipWallPolygon(const std::vector<WallPoint> &polygon,bool height,float limit,bool above) {
    std::vector<WallPoint> out;if(polygon.empty())return out;
    auto inside=[&](const WallPoint &p){float value=height?p.z:p.s;return above?value>=limit:value<=limit;};
    WallPoint a=polygon.back();
    for(const WallPoint &b:polygon) {
        if(inside(a)!=inside(b)) {
            float va=height?a.z:a.s,vb=height?b.z:b.s,t=(limit-va)/(vb-va);
            out.push_back({a.s+(b.s-a.s)*t,a.z+(b.z-a.z)*t,a.u+(b.u-a.u)*t,a.v+(b.v-a.v)*t});
        }
        if(inside(b))out.push_back(b);
        a=b;
    }
    return out;
}
// Whole-level lighting: a thing's outline thrown by a light onto the walls behind it, as the
// flashlight throws it (below), from wherever the light is: the picture stands across the
// light's way, is cast through its corners from the light, and lands on the walls its middle
// and its two sides are cast at, so it folds round a corner.
// Only as the floor shadow's other end: from a light above the feet, on a wall the floor
// shadow reaches, that stands on the thing's own floor. So the shadow starts at the feet,
// runs over the floor and up the wall in one piece, and never hangs on a wall by itself (a
// low light threw it high up a far wall, a lintel caught a piece of it).
void wallShadow(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const ShadowLight &light,float opacity) {
    if(!(thing.info->flags&MF_COUNTKILL)&&Game_ThingRole(thing.type)!=GAME_THING_UNCOUNTED_MONSTER)return;
    const float lamp[3]={light.x,light.y,light.z};
    float cz=z+units(thing.height)*0.5f,dx=x-lamp[0],dy=y-lamp[1],horizontal=std::hypot(dx,dy);
    if(horizontal<16||opacity<0.03f||lamp[2]<=z+8)return;
    // As far as the floor shadow runs (addEnemyShadow's length).
    const float floorReach=std::min(horizontal*units(thing.height)/(lamp[2]-z),192.0f)+4;
    float ux=dx/horizontal,uy=dy/horizontal,rx=uy,ry=-ux;
    float left=-(float)image.left,right=left+image.width,top=z+image.top,bottom=top-image.height;
    WallHit hits[3];int count=0;
    for(float side:{0.0f,left,right}) {
        float ox=x+rx*side,oy=y+ry*side,px=ox-lamp[0],py=oy-lamp[1],far=std::hypot(px,py);
        if(far<1)continue;
        WallHit hit;
        if(!firstSolidWall(ox,oy,z+8,px/far,py/far,0,floorReach,hit))continue;
        bool known=false;
        for(int n=0;n<count;++n)known=known||(hits[n].line==hit.line&&hits[n].side==hit.side&&hits[n].section==hit.section);
        if(!known)hits[count++]=hit;
    }
    constexpr int columns=6,rows=8;
    unsigned mode=MODE_HARD_SILHOUETTE|(settings.stippledShadows?MODE_STIPPLED:0);
    for(int h=0;h<count;++h) {
        WallSpan span;
        if(!wallSpan(lines[hits[h].line],hits[h].side,hits[h].section,span)||span.bottom>z+24)continue;
        float shown=opacity;   // as dark as on the floor: one shadow
        float sx=span.b.x-span.a.x,sy=span.b.y-span.a.y,len=std::hypot(sx,sy);if(len<1)continue;
        float tx=sx/len,ty=sy/len,nx=ty,ny=-tx;
        float planeDistance=(span.a.x-lamp[0])*nx+(span.a.y-lamp[1])*ny;
        std::array<WallPoint,(columns+1)*(rows+1)> grid;std::array<bool,(columns+1)*(rows+1)> valid;
        for(int j=0;j<=rows;++j)for(int i=0;i<=columns;++i) {
            float across=left+(right-left)*i/columns,height=top-(top-bottom)*j/rows;
            float px=x+rx*across-lamp[0],py=y+ry*across-lamp[1],pz=height-lamp[2];
            float toward=px*nx+py*ny,scale=toward<-1e-3f?planeDistance/toward:-1;
            int k=j*(columns+1)+i;valid[k]=scale>=1&&scale<12;
            float qx=lamp[0]+px*scale,qy=lamp[1]+py*scale;
            grid[k]={(qx-span.a.x)*tx+(qy-span.a.y)*ty,lamp[2]+pz*scale,(flip?1-(float)i/columns:(float)i/columns)*image.width,(float)j/rows*image.height};
        }
        float lift=2*decalLift(span.a.x+tx*len*0.5f,span.a.y+ty*len*0.5f,camera)+0.6f;
        auto &out=decalBatches[lump];
        for(int j=0;j<rows;++j)for(int i=0;i<columns;++i) {
            int k[4]={j*(columns+1)+i,j*(columns+1)+i+1,(j+1)*(columns+1)+i+1,(j+1)*(columns+1)+i};
            if(!valid[k[0]]||!valid[k[1]]||!valid[k[2]]||!valid[k[3]])continue;
            std::vector<WallPoint> cell={grid[k[0]],grid[k[1]],grid[k[2]],grid[k[3]]};
            cell=clipWallPolygon(cell,false,0,true);cell=clipWallPolygon(cell,false,len,false);
            cell=clipWallPolygon(cell,true,span.bottom,true);cell=clipWallPolygon(cell,true,span.top,false);
            auto vertex=[&](const WallPoint &p){return Vertex{span.a.x+tx*p.s+nx*lift,span.a.y+ty*p.s+ny*lift,p.z,p.u,p.v,shown,mode};};
            for(size_t n=1;n+1<cell.size();++n)for(size_t m:{size_t(0),n,n+1})out.push_back(vertex(cell[m]));
        }
    }
}
// Flashlight silhouettes: the beam's light sits right of the eye
// (collectFlashes), so a thing it catches throws its sprite's outline onto
// the wall behind it, enlarged with distance. The
// outline is projected through a grid of cells clipped to the wall's span
// and drawn hard-edged.
void flashlightSilhouette(const mobj_t &thing,const Image &image,int lump,bool flip,float x,float y,float z,const Uniforms &camera,const Flash &beam,const RagdollCard *card) {
    if(card&&(card->u1<=card->u0||card->v1<=card->v0))return;
    const float *lamp=beam.position;
    float cz=card?card->top-(card->v0+card->v1)*0.5f:z+units(thing.height)*0.5f;
    float dx=x-lamp[0],dy=y-lamp[1],horizontal=std::hypot(dx,dy);
    if(horizontal<16)return;
    float ex=x-camera.eye[0],ey=y-camera.eye[1],ez=cz-camera.eye[2],distance=std::sqrt(ex*ex+ey*ey+ez*ez);
    if(distance>700||(ex*camera.forward[0]+ey*camera.forward[1]+ez*camera.forward[2])/distance<beam.direction[3])return;
    float ux=dx/horizontal,uy=dy/horizontal,slope=(cz-lamp[2])/horizontal;
    WallHit hit;WallSpan span;
    if(!firstSolidWall(x,y,cz,ux,uy,slope,384,hit)||!wallSpan(lines[hit.line],hit.side,hit.section,span))return;
    float reach=flashAt(x+ux*(hit.t-2),y+uy*(hit.t-2),cz+slope*(hit.t-2),beam)/beam.strength;
    float opacity=std::min(0.6f,reach*1.4f)*std::clamp(1.25f-lighting(sides[lines[hit.line].sidenum[hit.side]].sector),0.3f,1.0f);
    if(opacity<0.03f)return;
    float sx=span.b.x-span.a.x,sy=span.b.y-span.a.y,len=std::hypot(sx,sy);if(len<1)return;
    float tx=sx/len,ty=sy/len,nx=ty,ny=-tx;
    float planeDistance=(span.a.x-lamp[0])*nx+(span.a.y-lamp[1])*ny;
    float rx,ry;cardAcross(camera,rx,ry);
    float u0=card?card->u0:0,v0=card?card->v0:0,u1=card?card->u1:image.width,v1=card?card->v1:image.height;
    float left=card?u0-card->half:-(float)image.left,right=left+(u1-u0);
    float top=card?card->top-v0:z+image.top,bottom=top-(v1-v0);
    constexpr int columns=6,rows=8;
    std::array<WallPoint,(columns+1)*(rows+1)> grid;std::array<bool,(columns+1)*(rows+1)> valid;
    for(int j=0;j<=rows;++j)for(int i=0;i<=columns;++i) {
        float across=left+(right-left)*i/columns,height=top-(top-bottom)*j/rows;
        float px=x+rx*across-lamp[0],py=y+ry*across-lamp[1],pz=height-lamp[2];
        float toward=px*nx+py*ny,scale=toward<-1e-3f?planeDistance/toward:-1;
        int k=j*(columns+1)+i;valid[k]=scale>=1;
        float qx=lamp[0]+px*scale,qy=lamp[1]+py*scale;
        grid[k]={(qx-span.a.x)*tx+(qy-span.a.y)*ty,lamp[2]+pz*scale,u0+(flip?1-(float)i/columns:(float)i/columns)*(u1-u0),v0+(float)j/rows*(v1-v0)};
    }
    float lift=2*decalLift(span.a.x+tx*len*0.5f,span.a.y+ty*len*0.5f,camera)+0.6f; // Above wall blood.
    auto &out=decalBatches[lump];
    unsigned mode=MODE_HARD_SILHOUETTE|(settings.stippledShadows?MODE_STIPPLED:0);
    for(int j=0;j<rows;++j)for(int i=0;i<columns;++i) {
        int k[4]={j*(columns+1)+i,j*(columns+1)+i+1,(j+1)*(columns+1)+i+1,(j+1)*(columns+1)+i};
        if(!valid[k[0]]||!valid[k[1]]||!valid[k[2]]||!valid[k[3]])continue;
        std::vector<WallPoint> cell={grid[k[0]],grid[k[1]],grid[k[2]],grid[k[3]]};
        cell=clipWallPolygon(cell,false,0,true);cell=clipWallPolygon(cell,false,len,false);
        cell=clipWallPolygon(cell,true,span.bottom,true);cell=clipWallPolygon(cell,true,span.top,false);
        auto vertex=[&](const WallPoint &p){return Vertex{span.a.x+tx*p.s+nx*lift,span.a.y+ty*p.s+ny*lift,p.z,p.u,p.v,opacity,mode};};
        for(size_t n=1;n+1<cell.size();++n)for(size_t m:{size_t(0),n,n+1})out.push_back(vertex(cell[m]));
    }
}
} // namespace doom3d
