/* Wound holes: where hitscan shots strike a monster, its sprite is shot
   through. Drawing only: the game's hitboxes are untouched. */
#include "internal.h"
#include "../wound_attachment.h"

namespace doom3d {
// Capture the texel struck before damage switches the monster to its pain
// or death drawing. Other frames attach to that original patch of artwork,
// never to the previous frame (which would accumulate drift). The tunnel
// follows the shot in the monster's local coordinates, including side hits.
struct Wound {
    float height,across,radius,yaw;
    const Image *source;
    doom_wounds::Point point;
    bool flipped;
    uint32_t seed;
    std::unordered_map<const Image*,doom_wounds::Point> frames;
    int tic=0;
    // Gore's skin suit (PLAN-skin-suit.md 6.1): the bone it struck (skeletons.cpp), how far along it, and how
    // far off its middle toward the drawing's right (a share of its flesh), so any drawing places it again.
    int bone=-1;float along=0,off=0;
    // How deep it goes (PLAN-skin-suit.md 6.2): each hit on the same spot one layer more, a heavy one two.
    // 1 the suit torn (flesh), 2 the bone (a limb's, the skull, the ribs; the belly's guts), 3 broken through
    // (the bone snapped, the brain, the ribs broken).
    int stage=1;
    bool burn=false;   // made by fire or plasma: charred, sealed, nothing flies (PLAN-skin-suit.md 7)
    int material=WOUND_FLESH;   // what the shot struck as painted there (woundMaterial): flesh, bone, metal
    float depth=0;   // the layers gone through, in halves: the pellets of one blast count half each
    int pellets=1;   // the pellets of one blast joined in it: from four a rip, bigger and ragged (PLAN-skin-suit.md 7)
};
static std::unordered_map<const mobj_t*,std::vector<Wound>> wounds;
static std::unordered_map<int,float> standing;
struct WoundSurface {
    const mobj_t *viewer;
    const Image *image;
    float x,y,feet,top,bottom,rx,ry,yaw;
    BodyPose pose;
    int tic;
    bool flipped,posed;
};
static std::unordered_map<const mobj_t*,WoundSurface> surfaces;
// A lying drawing's body: its opaque columns, the middle row of each, and which end the head is.
struct Lying { int first=-1,last=-1;bool headRight=true;std::vector<float> middle; };
static std::unordered_map<const Image*,Lying> lyings;
constexpr size_t maxWounds=10;
constexpr float maxRadius=3.0f;

void woundView(const mobj_t *thing,const mobj_t *viewer,const Image &image,bool flipped,float x,float y,float feet,
               float top,float bottom,float rx,float ry,const BodyPose *pose) {
    if(!settings.wounds||!thing||!thing->info||thing->health<=0)return;
    if(!(thing->info->flags&MF_COUNTKILL)&&Game_ThingRole(thing->type)!=GAME_THING_UNCOUNTED_MONSTER)return;
    surfaces[thing]={viewer,&image,x,y,feet,top,bottom,rx,ry,
        (float)(thing->angle*(2*doomPi/4294967296.0)),pose?*pose:BodyPose{},leveltime,flipped,pose!=nullptr};
}
bool woundSpriteView(const mobj_t &thing,SpriteView &out) {
    auto found=surfaces.find(&thing);
    if(found==surfaces.end()||!found->second.image||leveltime-found->second.tic>2)return false;
    const WoundSurface &s=found->second;
    out={s.image,s.x,s.y,s.top,s.bottom,s.rx,s.ry,s.flipped};
    return true;
}
static bool opaqueAt(const Image &image,doom_wounds::Point point) {
    int c=(int)std::floor(point.u),r=(int)std::floor(point.v);
    return c>=0&&r>=0&&c<image.width&&r<image.height&&image.pixels[((size_t)r*image.width+c)*2+1];
}
// Recreate the uncut surface only when a shot arrives. Its triangles carry
// the same UVs as the drawn strips, so intersection also inverts flinching.
static bool surfaceHit(const WoundSurface &surface,const mobj_t &shooter,float x,float y,float z,
                       float dx,float dy,float slope,doom_wounds::Point &point) {
    const Image &image=*surface.image;
    float left=-(float)image.left,right=left+image.width;
    float u0=surface.flipped?(float)image.width:0,u1=surface.flipped?0:(float)image.width;
    std::vector<Vertex> vertices;
    if(surface.posed)
        bodyQuads(vertices,surface.pose,{},surface.x,surface.y,surface.feet,surface.rx,surface.ry,
                  left,right,image.top,image.height,u0,u1);
    else
        quad(vertices,{surface.x+surface.rx*left,surface.y+surface.ry*left,surface.top,u0,0},
                      {surface.x+surface.rx*right,surface.y+surface.ry*right,surface.top,u1,0},
                      {surface.x+surface.rx*right,surface.y+surface.ry*right,surface.bottom,u1,(float)image.height},
                      {surface.x+surface.rx*left,surface.y+surface.ry*left,surface.bottom,u0,(float)image.height});
    // Start at the shooter, rather than the puff: a flinching body can bend
    // toward the eye far enough to be in front of the game's blood point.
    float sx=units(shooter.x),sy=units(shooter.y),travel=(x-sx)*dx+(y-sy)*dy;
    using Vec=std::array<float,3>;
    Vec origin={sx,sy,z-travel*slope},direction={dx,dy,slope};
    auto cross=[](const Vec &a,const Vec &b)->Vec {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
    auto dot=[](const Vec &a,const Vec &b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    float nearest=INFINITY;bool hit=false;
    for(size_t i=0;i+2<vertices.size();i+=3) {
        const Vertex &a=vertices[i],&b=vertices[i+1],&c=vertices[i+2];
        Vec e1={b.x-a.x,b.y-a.y,b.z-a.z},e2={c.x-a.x,c.y-a.y,c.z-a.z};
        Vec p=cross(direction,e2);float determinant=dot(e1,p);
        if(std::abs(determinant)<1e-6f)continue;
        Vec offset={origin[0]-a.x,origin[1]-a.y,origin[2]-a.z};
        float u=dot(offset,p)/determinant;
        if(u<0||u>1)continue;
        Vec q=cross(offset,e1);float v=dot(direction,q)/determinant;
        if(v<0||u+v>1)continue;
        float t=dot(e2,q)/determinant;
        if(t<0||t>=nearest)continue;
        doom_wounds::Point at={a.u+(b.u-a.u)*u+(c.u-a.u)*v,a.v+(b.v-a.v)*u+(c.v-a.v)*v};
        if(!opaqueAt(image,at))continue;
        nearest=t;point=at;hit=true;
    }
    return hit;
}

static doom_wounds::Frame art(const Image &image) {
    return {image.width,image.height,image.left,image.top,image.pixels.data()};
}
static doom_wounds::Point attached(Wound &wound,const Image &image) {
    if(wound.source==&image)return wound.point;
    auto found=wound.frames.find(&image);
    if(found!=wound.frames.end())return found->second;
    return wound.frames[&image]=doom_wounds::map(art(*wound.source),art(image),wound.point);
}
// Project an attached patch from its entry side to the current billboard.
static doom_wounds::Point projected(Wound &wound,const Image &image,bool flipped,float facing) {
    auto point=attached(wound,image);
    float left=-(float)image.left,right=left+image.width;
    float across=wound.flipped?right-point.u:left+point.u;
    point.u=flipped?right-across*facing:across*facing-left;
    return point;
}

// Gore: a texel of a drawing with a skeleton given to the wound as bone coordinates (none: -1).
static void boneOf(Wound &wound,const Image &image,doom_wounds::Point point) {
    wound.bone=-1;
    const SpriteSkeleton *s=goreOn()?spriteSkeleton(image):nullptr;
    int c=(int)std::floor(point.u),r=(int)std::floor(point.v);
    if(!s||c<0||r<0||c>=s->width||r>=s->height)return;
    int bone=s->owner[(size_t)r*s->width+c];
    if(bone<0)return;
    float across;skeletonPlace(*s,bone,point.u-image.left,image.top-point.v,wound.along,across);
    // Across as a share of the bone's flesh: wounds show only to views near the one that made them
    // (woundHoles), so the drawing's right stays the same side of the bone.
    wound.off=std::clamp(across/std::max(0.5f,s->radius[bone]),-1.0f,1.0f);
    wound.bone=bone;
}
// Where a wound on a bone lies in a drawing with a skeleton; false where that bone doesn't show there.
static bool bonePoint(Wound &wound,const SpriteSkeleton &s,const Image &image,doom_wounds::Point &point) {
    if(wound.bone<0||!s.has[wound.bone])return false;
    // Off the middle as far as it was, else nearer it: this drawing's flesh may be thinner there.
    bool shown=false;
    for(float share:{0.8f,0.4f,0.0f}) {
        float across=wound.off*share*s.radius[wound.bone],u,h;
        skeletonPoint(s,wound.bone,wound.along,across,u,h);
        point={u+image.left,image.top-h};
        int c=(int)std::floor(point.u),r=(int)std::floor(point.v);
        if(c<0||r<0||c>=s.width||r>=s.height)continue;
        int owner=s.owner[(size_t)r*s.width+c];
        if(owner>=0&&skeletonBonesJoin(owner,wound.bone)) {shown=true;break;}
    }
    if(!shown)return false;   // hidden behind another part: not here
    // The bone is a guess a few texels wide (the joints as placed, the bone's tilt): from it, the patch of
    // art the hole was made in is looked for close by (wound_attachment.h), so the hole sticks to the
    // painting as the drawings change instead of hanging still while the body moves under it. Only from
    // a drawing seen from near the same side: across a turn the paint doesn't match.
    if(wound.source&&wound.source!=&image) {
        auto cached=wound.frames.find(&image);
        if(cached!=wound.frames.end()) {point=cached->second;return true;}
        const SpriteSkeleton *from=spriteSkeleton(*wound.source);
        float turn=from?std::abs(std::remainder(from->yaw-s.yaw,2*(float)doomPi)):doomPi;
        if(turn<=0.9f)point=doom_wounds::mapNear(art(*wound.source),art(image),wound.point,point,4);
        wound.frames[&image]=point;
    } else if(wound.source==&image)point=wound.point;
    return true;
}

// What a hit cut (woundLastCut, for the spray: spray.cpp throws its bits): the layer its wound reached.
static int cut=CUT_NONE;
static int lastMaterial=0;   // (WOUND_FLESH) what the last hit struck (materialAt)
static int materialAt(const mobj_t *target,const Image &image,doom_wounds::Point point);
static int cutOf(const Wound &w) {
    if(w.material==WOUND_METAL)return CUT_NONE;   // nothing of it flies but sparks (blood.cpp)
    if(w.material==WOUND_BONE)return w.bone==SB_HEAD?CUT_SKULL:CUT_BONE;
    if(w.bone<0||!goreOn())return CUT_FLESH;
    bool head=w.bone==SB_HEAD,chest=w.bone==SB_SPINE&&w.along>0.45f,belly=w.bone==SB_PELVIS||(w.bone==SB_SPINE&&!chest);
    if(head)return w.stage>=2?CUT_BRAIN:CUT_SKULL;
    if(chest)return CUT_BONE;
    if(belly)return w.stage>=2?CUT_GUTS:CUT_FLESH;
    return std::abs(w.off)<=0.55f?CUT_BONE:CUT_FLESH;   // a limb: near its middle line the bone (wounds' toBone)
}

static void addWound(const mobj_t *target,const Image &image,doom_wounds::Point point,bool flipped,float yaw,int damage);
// The hit being made (woundHit sets them for addWound): by fire or plasma, by a blade.
static bool burning=false,cutting=false;
void woundBurn(mobj_t *target,const mobj_t *missile,const mobj_t *source,int damage) {
    if(!missile)return;
    burning=true;
    float len=std::max(1e-3f,std::hypot(units(missile->momx),units(missile->momy)));
    fixed_t slope=(fixed_t)(units(missile->momz)/len*FRACUNIT);
    woundHit(target,missile->x,missile->y,missile->z+(missile->height>>1),missile->momx,missile->momy,slope,damage,source);
    burning=false;
}
void woundHit(const mobj_t *target,fixed_t fx,fixed_t fy,fixed_t fz,fixed_t fdx,fixed_t fdy,fixed_t slope,int damage,const mobj_t *shooter) {
    cut=CUT_NONE;lastMaterial=WOUND_FLESH;
    cutting=Game_MeleeWeapon(shooter)==2;   // a chainsaw: deep at once
    if(!settings.wounds||!target||!target->info||bloodTint(target)==BLOOD_NONE)return;   // nothing to bleed: no bloody hole either
    if(!(target->info->flags&MF_COUNTKILL)&&Game_ThingRole(target->type)!=GAME_THING_UNCOUNTED_MONSTER)return;
    // Dying (the killing volley's pellets that go on through it, body_burst.cpp): with no drawing of it on
    // screen to aim at, the one its body falls as (ragdollLump), not the death frame it has just switched to.
    int spriteNum=target->sprite,frameNum=target->frame;
    if(target->health<=0&&target->info->painstate>0) {spriteNum=states[target->info->painstate].sprite;frameNum=states[target->info->painstate].frame;}
    if(spriteNum<0||spriteNum>=numsprites)return;
    if(Game_MeleeWeapon(shooter)==1)return;   // a punch bruises, it doesn't shoot a hole through (the user: "a not hole for the damn fists")
    const auto &sprite=sprites[spriteNum];int frame=frameNum&FF_FRAMEMASK;
    if(frame>=sprite.numframes)return;
    float dx=units(fdx),dy=units(fdy),length=std::hypot(dx,dy);
    if(length<1e-4f)return;
    dx/=length;dy/=length;
    float viewYaw=std::atan2(dy,dx),sourceYaw;
    float a=(float)(target->angle*(2*doomPi/4294967296.0));
    const Image *source=nullptr;bool flipped=false;doom_wounds::Point point={};
    auto drawn=surfaces.find(target);
    if(shooter&&drawn!=surfaces.end()&&drawn->second.viewer==shooter&&leveltime-drawn->second.tic<=2) {
        const WoundSurface &surface=drawn->second;
        if(!surfaceHit(surface,*shooter,units(fx),units(fy),units(fz),dx,dy,units(slope),point))return;
        source=surface.image;flipped=surface.flipped;sourceYaw=surface.yaw;
        viewYaw=std::atan2(surface.rx,-surface.ry);
    } else {
        // Off-screen hits use the attacker's view. Frame selection is based
        // on the bearing to the body's center, not an off-center pellet ray.
        float bearing=shooter?std::atan2(units(target->y)-units(shooter->y),units(target->x)-units(shooter->x)):viewYaw;
        angle_t angle=(angle_t)(int64_t)(bearing*(4294967296.0/(2*doomPi)));
        const auto &sf=sprite.spriteframes[frame];
        // As it is drawn facing (turning, or rolled over in the air: p_swept.c), or the hole lands on another view.
        unsigned drawn=target->angle;Game_DrawAngle(target,&drawn);
        unsigned rotation=sf.rotate?(angle-(angle_t)drawn+(unsigned)(ANG45/2)*9)>>29:0;
        source=&lumpImage(firstspritelump+sf.lump[rotation],false);flipped=sf.flip[rotation];sourceYaw=a;
        if(source->pixels.empty())return;
        if(shooter&&shooter->player)viewYaw=(float)(shooter->angle*(2*doomPi/4294967296.0));
        float nx=std::cos(viewYaw),ny=std::sin(viewYaw),along=dx*nx+dy*ny;
        if(std::abs(along)<1e-4f)return;
        float ox=units(fx)-units(target->x),oy=units(fy)-units(target->y),t=-(ox*nx+oy*ny)/along;
        float height=units(fz)-units(target->z)+t*units(slope);
        float across=(ox+dx*t)*ny-(oy+dy*t)*nx;
        float left=-(float)source->left,right=left+source->width;
        point={flipped?right-across:across-left,source->top-height};
    }
    lastMaterial=materialAt(target,*source,point);
    addWound(target,*source,point,flipped,viewYaw-sourceYaw,damage);
}

// A wound at point of image (a drawing of target), as a hit from yaw (the view's turn from the drawing's):
// on its bone where the drawing has a skeleton, joining a wound it lands on (a layer deeper), else a new one.
// What a body is made of where a shot strikes it, read off the paint (Game_BodyMaterials: the revenant): bone
// (his tan arms, shins and skull), flesh (the red muscle left on his thighs and belly), metal (the white
// armour on his chest and shoulders). Near-black edges and shadows go by the texels round them.
static int paintMaterial(byte index) {
    const byte *pal=playpal()+index*3;
    int r=pal[0],g=pal[1],b=pal[2],top=std::max({r,g,b}),low=std::min({r,g,b});
    if(top<24)return -1;
    if(r>2.2f*g&&r>2*b)return WOUND_FLESH;
    if(top-low<0.2f*top)return WOUND_METAL;
    return WOUND_BONE;
}
static int materialAt(const mobj_t *target,const Image &image,doom_wounds::Point point) {
    if(!goreOn()||!Game_BodyMaterials(target->type))return WOUND_FLESH;
    int c0=(int)std::floor(point.u),r0=(int)std::floor(point.v),votes[3]={0,0,0};
    for(int reach=0;reach<=2;++reach) {
        for(int r=r0-reach;r<=r0+reach;++r)for(int c=c0-reach;c<=c0+reach;++c) {
            if(c<0||r<0||c>=image.width||r>=image.height)continue;
            size_t i=((size_t)r*image.width+c)*2;
            if(!image.pixels[i+1])continue;
            int m=paintMaterial(image.pixels[i]);
            if(m>=0)++votes[m];
        }
        if(votes[0]+votes[1]+votes[2])break;
    }
    return votes[WOUND_METAL]>votes[WOUND_BONE]&&votes[WOUND_METAL]>votes[WOUND_FLESH]?WOUND_METAL:
           votes[WOUND_FLESH]>votes[WOUND_BONE]?WOUND_FLESH:WOUND_BONE;
}
int woundLastMaterial() {return lastMaterial;}

static void addWound(const mobj_t *target,const Image &image,doom_wounds::Point point,bool flipped,float yaw,int damage) {
    if(!opaqueAt(image,point))return;
    float height=image.top-point.v,across=flipped?image.width-image.left-point.u:point.u-image.left;
    uint32_t seed=hash32((uint32_t)std::lround(point.u*16)*73856093u^(uint32_t)std::lround(point.v*16)*19349663u^
                         (uint32_t)leveltime*83492791u^(uint32_t)target->type*2654435761u);
    Wound wound={height,across,std::clamp(0.7f+damage/14.0f,0.8f,1.8f),yaw,&image,point,flipped,seed,{},leveltime};
    boneOf(wound,image,point);
    bodyJolt(target,wound.bone,damage);
    fatHit(target,point.u-image.left,height,damage);
    int deeper=1+(damage>=15||cutting);   // a heavy hit (a 15-damage pellet or more) or a blade goes two layers
    wound.burn=burning;
    wound.material=lastMaterial;
    if(cutting)wound.radius=std::min(maxRadius,wound.radius*1.4f);
    wound.depth=deeper-1.0f;wound.stage=std::min(3,deeper);
    auto &list=wounds[target];
    // Compare holes on this drawing, so hits on a moving limb still merge. On a drawing with a skeleton the
    // old one is placed by its bone (the monster flinched into its pain drawing between the shots).
    const SpriteSkeleton *skeleton=goreOn()?spriteSkeleton(image):nullptr;
    auto where=[&](Wound &w,doom_wounds::Point &at) {
        float facing=std::cos(w.yaw-wound.yaw);
        // On a bone, where it lands decides; only a shot from the far side is another wound.
        if(skeleton&&w.bone>=0)return facing>=0.3f&&bonePoint(w,*skeleton,image,at);
        if(facing<0.8f)return false;
        at=projected(w,image,flipped,facing);
        return true;
    };
    auto distance=[&](Wound &w) {
        doom_wounds::Point at;
        return where(w,at)?std::hypot(at.u-point.u,at.v-point.v):1e9f;
    };
    for(Wound &w:list) {
        // Pellets of the same blast (this tic) join from further apart, into one ragged tear: a shotgun up close
        // tears one deep hole, not a ring of small ones (PLAN-skin-suit.md 7).
        bool volley=w.tic==leveltime;
        if(distance(w)<(w.radius+wound.radius)*(volley?1.6f:0.9f)) {
            doom_wounds::Point at;where(w,at);
            doom_wounds::Point middle={(at.u+point.u)*0.5f,(at.v+point.v)*0.5f};
            if(!opaqueAt(image,middle))continue;
            wound.radius=std::min(maxRadius,std::sqrt(w.radius*w.radius+wound.radius*wound.radius));
            wound.point=middle;wound.height=image.top-middle.v;
            wound.across=flipped?image.width-image.left-middle.u:middle.u-image.left;
            wound.seed=w.seed;wound.burn=wound.burn||w.burn;
            boneOf(wound,image,middle);
            wound.depth=w.depth+(volley?0.5f:1.0f)*deeper;   // the same spot again: a layer deeper (a pellet half one)
            wound.stage=std::min(3,1+(int)std::floor(wound.depth));
            // A volley's pellets join into one tear that keeps growing with each: a super shotgun up close rips one
            // hole a third of an imp across (capped at 3.8 the twentieth pellet left it the size of the fourth's).
            wound.pellets=volley?w.pellets+1:1;
            if(volley)wound.radius=std::min(maxRadius+0.8f+0.22f*std::max(0,wound.pellets-3),std::max(wound.radius,w.radius+0.35f));
            if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorewound joined bone %d stage %d radius %.1f\n",wound.bone,wound.stage,wound.radius);
            w=std::move(wound);
            cut=cutOf(w);
            return;
        }
    }
    if(list.size()>=maxWounds) {
        auto nearest=std::min_element(list.begin(),list.end(),[&](Wound &p,Wound &q) {return distance(p)<distance(q);});
        nearest->radius=std::min(maxRadius,nearest->radius+0.3f);nearest->tic=leveltime;nearest->stage=std::min(3,nearest->stage+1);
        cut=cutOf(*nearest);
        return;
    }
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"gorewound new bone %d stage %d radius %.1f\n",wound.bone,wound.stage,wound.radius);
    cut=cutOf(wound);
    list.push_back(wound);
}
int woundLastCut() {return cut;}
void forgetWounds(const mobj_t *thing) {wounds.erase(thing);surfaces.erase(thing);forgetCraters(thing);forgetStains(thing);forgetJolt(thing);forgetWobble(thing);}
int woundSpots(const mobj_t *thing,WoundSpot *out,int most,bool burns) {
    auto found=wounds.find(thing);
    if(found==wounds.end())return 0;
    int count=0;
    for(const Wound &w:found->second) {
        if(count>=most)break;
        if(w.burn!=burns)continue;   // (burnt shut: it doesn't drip; it smokes, spray.cpp)
        if(!burns&&w.material!=WOUND_FLESH)continue;   // bone and metal don't bleed
        out[count++]={w.height,w.across,w.yaw,w.radius,w.tic};
    }
    return count;
}
bool woundsOnlyBurnt(const mobj_t *thing) {
    auto found=wounds.find(thing);
    if(found==wounds.end()||found->second.empty())return false;
    for(const Wound &w:found->second)if(!w.burn)return false;
    return true;
}
// scp_7179_b is real
void woundsShutdown() {wounds.clear();surfaces.clear();lyings.clear();standing.clear();cratersShutdown();stainsShutdown();joltsShutdown();wobblesShutdown();}

// How tall a monster stands, by its first drawing, to tell lying death drawings.
float standingTop(const mobj_t &thing) {
    auto found=standing.find(thing.type);
    if(found!=standing.end())return found->second;
    float top=0;
    int state=thing.info->spawnstate;
    if(state>0&&states[state].sprite>=0&&states[state].sprite<numsprites) {
        const auto &sprite=sprites[states[state].sprite];int frame=states[state].frame&FF_FRAMEMASK;
        if(frame<sprite.numframes)top=(float)lumpImage(firstspritelump+sprite.spriteframes[frame].lump[0],false).top;
    }
    return standing[thing.type]=top;
}

static const Lying &lyingOf(const Image &image) {
    auto found=lyings.find(&image);
    if(found!=lyings.end())return found->second;
    Lying &l=lyings[&image];
    int w=image.width,h=image.height;
    l.middle.assign(w,-1);
    double upper=0,lower=0;int upperCount=0,lowerCount=0;
    for(int c=0;c<w;++c) {
        int top=-1,bottom=-1;
        for(int r=0;r<h;++r) if(image.pixels[((size_t)r*w+c)*2+1]) {
            if(top<0)top=r;bottom=r;
            if(r<h/2){upper+=c;++upperCount;}else{lower+=c;++lowerCount;}
        }
        if(top<0)continue;
        if(l.first<0)l.first=c;l.last=c;
        l.middle[c]=(top+bottom+1)*0.5f;
    }
    l.headRight=!upperCount||!lowerCount||upper/upperCount>=lower/lowerCount;
    return l;
}

// Wound-local marks, held for the wound's lifetime: a narrow dark torn
// edge, ragged red flesh, a few brighter lip texels, and a mottled stain.
// The short trail runs down the source drawing and bends with the body.
static byte woundMark(float x,float y,float radius,uint32_t seed) {
    float d=std::hypot(x,y);
    if(d<=radius)return 1;
    if(d<=radius+0.55f)return 2;
    float angle=std::atan2(y,x),phase=(seed&1023u)*(2*doomPi/1024);
    float edge=radius+1.15f+0.35f*std::sin(3*angle+phase)+0.2f*std::sin(7*angle-phase);
    uint32_t cell=hash32(seed^(uint32_t)(int)std::lround(x)*374761393u^(uint32_t)(int)std::lround(y)*668265263u);
    float grain=(cell&255u)/255.0f;
    if(d<=edge) {
        // A small broken highlight along the upper lip, never a shiny ring.
        return y<-0.25f&&x<0.4f&&d>edge-0.7f&&grain>0.35f?4:3;
    }
    byte stain=d<=edge+0.8f&&grain>0.3f?5:0;
    float length=radius+2.5f+(seed%3u)*0.5f;
    if(y>radius*0.5f&&y<length) {
        float progress=(y-radius*0.5f)/(length-radius*0.5f);
        float middle=((int)((seed>>10)&3u)-1.5f)*0.25f+std::sin(y*0.8f+phase)*0.2f;
        if(std::abs(x-middle)<0.35f*(1-progress)+0.55f)return progress<0.75f?3:5;
    }
    return stain;
}

static bool woundHoles(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask);
// -skeletonshow: each bone of the drawing's skeleton painted over it in a colour of its own (skeletons.cpp).
static bool skeletonShown(const Image &image,std::vector<byte> &mask,bool any) {
    static const int show=M_CheckParm((char*)"-skeletonshow");
    const SpriteSkeleton *s=show?spriteSkeleton(image):nullptr;
    if(!s)return false;
    if(!any)mask.assign((size_t)image.width*image.height,0);
    // The bone map (bone_map.cpp) as the skeleton under the skin, the bones' lines over it.
    if(s->boneMap.size()==mask.size())
        for(size_t i=0;i<mask.size();++i)if(s->boneMap[i]!=BONE_NONE)mask[i]=boneMapColour(s->boneMap[i]);
    if(show&&myargc>show+1&&!strcmp(myargv[show+1],"bones"))return true;   // -skeletonshow bones: the map alone
    static const byte colours[SKELETON_BONES]={231,251,160,112,200,118,204,176,208,182,213};
    for(int b=0;b<SKELETON_BONES;++b) {
        if(!s->has[b])continue;
        const float *p=s->joint[skeletonBoneEnds[b][0]],*q=s->joint[skeletonBoneEnds[b][1]];
        for(int k=0;k<=24;++k) {
            float t=k/24.0f;int c=(int)std::floor(p[0]+(q[0]-p[0])*t+image.left),r=(int)std::floor(image.top-(p[1]+(q[1]-p[1])*t));
            if(c>=0&&r>=0&&c<image.width&&r<image.height&&image.pixels[((size_t)r*image.width+c)*2+1])mask[(size_t)r*image.width+c]=colours[b];
        }
    }
    return true;
}
bool woundMask(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask) {
    if(!settings.wounds)return false;
    bool holes=woundHoles(thing,image,flipped,rx,ry,mask);
    holes=skeletonShown(image,mask,holes)||holes;
    holes=stainMask(thing,image,flipped,rx,ry,mask)||holes;   // blood from others (body_stains.cpp)
    // Nukage rain's bites over them (craters.cpp).
    return craterMask(thing,image,mask,holes)||holes;
}

// A burn's colours (the palette's nearest): 0 char black, 1 cooked dark meat, 2 scorched brown, 3 soot grey,
// 4 charred bone, 5 an ember.
static byte burnColour(int which) {
    static byte colours[6]={0,0,0,0,0,0};
    if(!colours[0]) {
        const byte *pal=playpal();
        static const int want[6][3]={{14,10,8},{48,22,14},{70,46,28},{52,50,48},{96,90,80},{200,70,12}};
        for(int k=0;k<6;++k) {
            int best=16,d0=INT_MAX;
            for(int n=16;n<256;++n) {int dr=pal[n*3]-want[k][0],dg=pal[n*3+1]-want[k][1],db=pal[n*3+2]-want[k][2],d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=n;}}
            colours[k]=(byte)best;
        }
    }
    return colours[std::clamp(which,0,5)];
}
// The colours of a hit on bone or metal (the palette's nearest): 0 a crack's dark, 1 bone chipped pale, 2 a
// dent's pit, 3 its bright scratch, 4 scuffed metal.
constexpr float fatWoundMost=5;   // the widest a fat wound opens, however often the spot is hit
// Fat (Game_BodyFat): pale yellow, a lighter and a darker one for grain.
static byte fatColour(int which) {
    static byte colours[3]={0,0,0};
    if(!colours[0]) {
        const byte *pal=playpal();
        static const int want[3][3]={{226,200,128},{244,226,164},{176,140,76}};
        for(int k=0;k<3;++k) {
            int best=16,d0=INT_MAX;
            for(int n=16;n<256;++n) {int dr=pal[n*3]-want[k][0],dg=pal[n*3+1]-want[k][1],db=pal[n*3+2]-want[k][2],d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=n;}}
            colours[k]=(byte)best;
        }
    }
    return colours[which];
}
static byte materialColour(int which) {
    static byte colours[5]={0,0,0,0,0};
    if(!colours[0]) {
        const byte *pal=playpal();
        static const int want[5][3]={{38,26,16},{176,150,116},{56,56,56},{232,232,232},{128,128,128}};
        for(int k=0;k<5;++k) {
            int best=16,d0=INT_MAX;
            for(int n=16;n<256;++n) {int dr=pal[n*3]-want[k][0],dg=pal[n*3+1]-want[k][1],db=pal[n*3+2]-want[k][2],d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=n;}}
            colours[k]=(byte)best;
        }
    }
    return colours[std::clamp(which,0,4)];
}
// Colours that never glisten: bone and brain (the skin suit's). Wet, a big pale patch blew out into a white
// blot that slid with the light and the eye, not the body.
static bool dryColour[256];

// The shot holes alone (woundMask).
static bool woundHoles(const mobj_t &thing,const Image &image,bool flipped,float rx,float ry,std::vector<byte> &mask) {
    auto found=wounds.find(&thing);
    if(found==wounds.end()||found->second.empty()||image.pixels.empty())return false;
    bool dead=thing.health<=0;
    // Death drawings face the viewer; lying ones carry the holes along the body.
    float tall=standingTop(thing);
    bool lying=dead&&image.top<0.85f*tall;
    const Lying *body=lying?&lyingOf(image):nullptr;
    if(lying&&body->first<0)return false;
    float a=(float)(thing.angle*(2*doomPi/4294967296.0));
    int w=image.width,h=image.height;
    mask.assign((size_t)w*h,0);
    bool any=false;
    // Gore's skin suit: on a drawing with a skeleton a wound sits on its bone, and under the torn suit is
    // flesh (the blood's ramp in stripes along the bone, as muscle) with the bone's pale line where the hole
    // reaches it: never shot through.
    const SpriteSkeleton *skeleton=goreOn()&&!lying?spriteSkeleton(image):nullptr;
    std::vector<bool> flesh;
    if(skeleton)flesh.assign((size_t)w*h,false);
    int tint=bloodTint(&thing);
    const auto &shades=bloodPalette(tint);
    const GutsPatch &guts=gutsOf(thing);
    // A ball of flesh (the cacodemon, Game_BodyBall) is flesh all through: no shot shows through it, the hole's
    // inside its own meat (in its blood's tint: a caco's blue).
    const bool solid=Game_BodyBall(thing.type)!=0;
    // A fat body: under the skin a thick layer of it, wider than the hole a thin body gets, the meat under that
    // showing through its middle as the same spot takes more hits. Nothing shows through him.
    const bool fat=goreOn()&&Game_BodyFat(thing.type)!=0;
    byte bone=guts.bone;
    if(skeleton&&bone>=16)dryColour[bone]=true;
    static byte brain[2]={0,0};
    if(skeleton&&!brain[0]) {   // the palette's pinks, for a brain
        const byte *pal=playpal();
        auto nearest=[&](int r,int g,int b) {int best=16,d0=INT_MAX;for(int i=16;i<256;++i) {int dr=pal[i*3]-r,dg=pal[i*3+1]-g,db=pal[i*3+2]-b,d=dr*dr+dg*dg+db*db;if(d<d0) {d0=d;best=i;}}return (byte)best;};
        brain[0]=nearest(222,140,150);brain[1]=nearest(160,80,92);
        dryColour[brain[0]]=dryColour[brain[1]]=true;
        for(int k=0;k<6;++k)dryColour[burnColour(k)]=true;   // burns are dry
        for(int k=0;k<5;++k)dryColour[materialColour(k)]=true;   // and broken bone and dented metal
    }
    for(Wound &wound:found->second) {
        float facing=std::sin(a+wound.yaw)*rx-std::cos(a+wound.yaw)*ry;
        doom_wounds::Point onBone;
        bool placed=skeleton&&wound.bone>=0;
        if(placed) {
            if(!dead&&facing<0.25f)continue;   // on the far side of the body: the suit's other side
            if(!bonePoint(wound,*skeleton,image,onBone))continue;
        } else if(!dead&&std::abs(facing)<0.4f)continue;
        // Procedural bends then carry these source texels through the same
        // bodyQuads/fallQuads deformation as the rest of the drawing.
        auto point=placed?onBone:dead?attached(wound,image):projected(wound,image,flipped,facing);
        float cu=point.u,cv=point.v;
        if(lying) {
            float along=std::clamp(wound.height/std::max(1.0f,tall),0.05f,0.95f)*(body->last+1-body->first);
            cu=body->headRight?body->first+along:body->last+1-along;
            int column=std::clamp((int)cu,body->first,body->last);
            if(body->middle[column]<0)continue;
            cv=body->middle[column]-wound.across*0.3f;
        }
        int c0=(int)std::floor(cu),r0=(int)std::floor(cv);
        if(c0<0||r0<0||c0>=w||r0>=h||!image.pixels[((size_t)r0*w+c0)*2+1])continue;   // missed the body here
        // Opened onto bone (the bone map under its middle), a wound is wider at once, so the bone's shape has
        // room to show (a rib or two and a gap, a piece of skull with its socket, the two shin bones).
        bool ontoBone=placed&&!skeleton->boneMap.empty()&&skeleton->boneMap[(size_t)r0*w+c0]!=BONE_NONE;
        float radius=placed?std::min(std::max(maxRadius+1.6f,wound.radius),wound.radius+(ontoBone?1.2f:0.0f)+0.6f*(wound.stage-1)):wound.radius;   // deeper, wider
        if(fat&&wound.material==WOUND_FLESH&&!wound.burn)radius=std::min(1.2f*radius+0.8f+0.4f*(wound.stage-1),fatWoundMost);
        float lobes=wound.pellets>=4?1.45f:1.0f;   // a rip's lobes reach further out
        int reach=(int)std::ceil(radius*lobes+2.5f),down=(int)std::ceil(radius*lobes+4);
        // What lies under the suit here: the head, the chest (the spine's upper half), the belly (its lower
        // half, the pelvis), or a limb.
        enum { LIMB,HEAD,CHEST,BELLY } region=wound.bone==SB_HEAD?HEAD:wound.bone==SB_PELVIS?BELLY:
            wound.bone==SB_SPINE?(wound.along>0.45f?CHEST:BELLY):LIMB;
        float boneLength=placed?std::hypot(skeleton->joint[skeletonBoneEnds[wound.bone][1]][0]-skeleton->joint[skeletonBoneEnds[wound.bone][0]][0],
                                           skeleton->joint[skeletonBoneEnds[wound.bone][1]][1]-skeleton->joint[skeletonBoneEnds[wound.bone][0]][1]):1;
        float direction=dead||placed?1.0f:((wound.flipped!=flipped)?-1.0f:1.0f)*(facing<0?-1.0f:1.0f);
        for(int r=std::max(0,r0-reach);r<=std::min(h-1,r0+down);++r)
            for(int c=std::max(0,c0-reach);c<=std::min(w-1,c0+reach);++c) {
                size_t i=(size_t)r*w+c;
                if(!image.pixels[i*2+1]||mask[i]==1||(skeleton&&flesh[i]))continue;
                float mx=(c+0.5f-cu)*direction,my=r+0.5f-cv;
                if(wound.pellets>=4) {   // a rip: lobed and torn, not a round punch
                    float a=std::atan2(my,mx),phase=(wound.seed>>12&1023u)*(2*doomPi/1024);
                    float lobe=1+0.24f*std::sin(3*a+phase)+0.13f*std::sin(5*a-2*phase)+0.1f*(hashUnit(c,r,wound.seed^0x6bu)-0.5f);
                    mx/=lobe;my/=lobe;
                }
                byte mark=woundMark(mx,my,radius,wound.seed);
                float spot=std::hypot(mx,my);   // from the middle, a rip's lobes taken out
                if(fat&&!wound.burn&&wound.material==WOUND_FLESH&&!shades.empty()) {
                    // A ragged edge, each its own (round ones stacked up like coins), the fat a band of a texel or two
                    // whatever the size, the meat inside it. Wounds that meet run together: fat paints over nothing.
                    float grain=hashUnit(c,r,wound.seed^0x5fu),turn=std::atan2(my,mx),phase=(wound.seed>>8&1023u)*(2*doomPi/1024);
                    float rim=radius*(1+0.2f*std::sin(3*turn+phase)+0.12f*std::sin(5*turn-2*phase))+0.5f*(hashUnit(c,r,wound.seed^0x33u)-0.5f);
                    if(spot<=rim) {
                        float meat=std::min(rim-1.3f-0.8f*hashUnit(c,r,wound.seed^0x17u),radius*(0.2f+0.2f*wound.stage));
                        size_t last=shades.size()-1;
                        if(spot<meat)mask[i]=shades[(size_t)std::lround((wound.stage>=3&&spot<0.5f*meat?0.06f:grain<0.5f?0.3f:0.46f)*last)];
                        else if(mask[i]<16)mask[i]=fatColour(grain<0.55f?0:grain<0.8f?1:2);
                        if(skeleton)flesh[i]=true;
                        any=true;
                        continue;
                    }
                }
                if(wound.burn&&mark) {
                    // Burnt (plasma, fire): sealed and charred. No blood running, no red lip: a black rim, a scorched
                    // ring round it, and inside cooked dark meat with a few embers (or the bone, charred grey).
                    float d=std::hypot(c+0.5f-cu,r+0.5f-cv),edge=radius+1.4f+0.5f*hashUnit(c,r,wound.seed^0x77u);
                    if(d>edge+1.2f)continue;   // no trail, no stain past the scorch
                    float grain=hashUnit(c,r,wound.seed^0x3cu);
                    byte colour=d>edge?burnColour(grain<0.5f?2:3):d>radius?burnColour(grain<0.7f?0:1):
                                (placed&&!skeleton->boneMap.empty()&&skeleton->boneMap[(size_t)r*w+c]!=BONE_NONE&&grain<0.6f)?burnColour(4):
                                burnColour(grain<0.12f?5:grain<0.6f?1:0);
                    if(mark==1&&!placed&&!solid&&d<=radius*0.7f)colour=0;   // without a skeleton: burnt through
                    if(colour>=16) {mask[i]=colour;if(skeleton)flesh[i]=true;any=true;continue;}
                    if(!colour) {mask[i]=1;any=true;continue;}
                }
                if(wound.material!=WOUND_FLESH&&!wound.burn&&mark) {
                    float d=std::hypot(c+0.5f-cu,r+0.5f-cv),grain=hashUnit(c,r,wound.seed^0x9du);
                    byte colour=0;
                    if(wound.material==WOUND_BONE) {
                        // Bone (the revenant's): no blood. Through the middle a thin bone snaps clean; round it the
                        // bone cracks, a few dark lines out from the break, the rest of its edge chipped pale.
                        if(d<=radius*0.75f) {mask[i]=1;any=true;continue;}
                        if(d>radius+1.6f)continue;
                        float a=std::atan2(r+0.5f-cv,c+0.5f-cu)/(2*doomPi)+0.5f;
                        float ray=std::fmod(a*5+(wound.seed&255u)/256.0f,1.0f);
                        colour=ray<0.12f||d<=radius+0.3f?materialColour(0):grain<0.35f?materialColour(1):0;
                    } else {
                        // Metal (his armour): dented, not holed. A dark pit, a bright scratch on its upper edge,
                        // scuffs round it.
                        float rd=radius*0.8f;
                        if(d>rd+1.0f)continue;
                        colour=d<=rd*0.6f?materialColour(2):d<=rd+0.3f&&r+0.5f<cv?materialColour(3):grain<0.5f?materialColour(4):0;
                    }
                    if(colour>=16) {mask[i]=colour;if(skeleton)flesh[i]=true;any=true;}
                    continue;
                }
                if(placed&&mark>=3) {
                    // Under the suit the inside shows: only a thin dark torn edge round it (a wide red lip ring
                    // swallowed the few texels inside), and while fresh (the first layer) the blood running down.
                    mark=spot<=radius+0.9f+0.4f*hashUnit(c,r,wound.seed^0x51u)?2:wound.stage==1&&r+0.5f>cv?mark:0;
                }
                if(placed&&mark==1&&!shades.empty()) {
                    float along,across;
                    skeletonPlace(*skeleton,wound.bone,c+0.5f-image.left,image.top-(r+0.5f),along,across);
                    float middle=spot/std::max(0.5f,radius);   // 0 the hole's middle, 1 its edge
                    float grain=hashUnit(c,r,wound.seed);
                    // Flesh: the blood's ramp in stripes along the bone, as muscle.
                    // (Bright against the torn edge's dark ring: at a few texels, dark on dark read as a smudge.)
                    // Dark and varied, as the sprite's own shading would have it: a bright flat red read as paint.
                    float shade=((int)std::floor(std::abs(across))%2?0.46f:0.3f)+0.12f*(hashUnit(c,r,wound.seed^0x2du)-0.5f);
                    byte colour=shades[(size_t)std::lround(std::clamp(shade,0.0f,1.0f)*(shades.size()-1))];
                    byte dark=shades[(size_t)std::lround(0.06f*(shades.size()-1))];
                    // Either the shot finds bone or it goes through (the user: plain fleshy wounds are boring).
                    // Bone: the head (the skull), the chest (the ribs), a limb struck near its middle line; each
                    // hit more breaks it further and the third shoots through what's left. Through: a limb's
                    // flesh off the bone, the belly; the belly shows its guts once it is torn open wider.
                    bool onAxis=std::abs(across)<0.75f;
                    // What lies under the skin here: the drawing's bone map (bone_map.cpp: the skull, the rib cage,
                    // the pelvis, the limbs' bones), else the old rule (near the bone's line). A wound whose middle
                    // finds bone opens onto it (the chest's cage always: ribs or the gaps between them); one in
                    // flesh goes straight through (the belly shows its guts once torn wider).
                    const auto &map=skeleton->boneMap;
                    auto under=[&](int cc,int rr) {
                        if(map.empty()||cc<0||rr<0||cc>=w||rr>=h)return -1;
                        return (int)map[(size_t)rr*w+cc];
                    };
                    int here=under(c,r),centre=under(c0,r0);
                    bool toBone=centre>=0?centre!=BONE_NONE||region==CHEST
                                         :region==HEAD||region==CHEST||(region==LIMB&&std::abs(wound.off)<=0.55f);
                    if(!toBone) {
                        if(!(region==BELLY&&wound.stage>=2&&middle<0.65f))goto through;
                        colour=gutsColour(guts,c,r,wound.seed);
                    }
                    else if(wound.stage>=3&&middle<0.3f)goto through;
                    else if(region==HEAD&&wound.stage>=2&&middle<0.55f)colour=brain[grain<0.35f];   // the skull open: the brain
                    else if(here>=0) {
                        // The map's bone, cracked from the second hit (dark lines through it), its holes dark.
                        // (Cracks only from the third hit, few: the bone's shape is what shows.)
                        if(here==BONE_SOLID||here==BONE_EDGE)colour=wound.stage>=3&&grain<0.15f?dark:boneMapColour(here);
                        else if(here==BONE_HOLE)colour=dark;
                    }
                    else if(region==LIMB)
                        colour=onAxis?(wound.stage>=2&&middle<0.45f?dark:bone)            // the bone; snapped: a gap
                              :wound.stage>=2&&std::abs(across)<1.3f&&grain<0.3f?bone:colour; // splinters beside it
                    else if(region==HEAD)
                        // The skull cracked open in pieces (not a pale disk), then the brain under it.
                        colour=middle<0.8f&&grain>0.3f?bone:dark;
                    else {
                        // Ribs across the spine every two units, broken here and there from the second hit.
                        bool rib=(int)std::floor(along*boneLength/2)%2==0;
                        colour=rib&&!(wound.stage>=2&&grain<0.35f)?bone:dark;
                    }
                    // Bone isn't clean: a third of it smeared with blood (all ivory, it read white; the user).
                    if((colour==bone||colour==boneMapColour(BONE_SOLID))&&hashUnit(c,r,wound.seed^0x5au)<0.3f)
                        colour=shades[(size_t)std::lround(0.45f*(shades.size()-1))];
                    if(colour>=16) {mask[i]=colour;flesh[i]=true;any=true;continue;}
                }
                through:
                if(solid&&mark==1) {
                    byte meat=gutsColour(guts,c,r,wound.seed);
                    if(hashUnit(c,r,wound.seed^0x41u)<0.25f)meat=bloodRemap(guts.dark,guts.tint);   // dark in its depths here and there
                    if(meat>=16) {mask[i]=meat;if(skeleton)flesh[i]=true;any=true;continue;}
                }
                // Nearby stains combine, but never paint over a hole or its
                // recessed edge. Highlights win over the red lip and stain.
                static const byte priority[6]={0,5,4,2,3,1};
                if(placed&&wound.stage>=2&&mark==5)mark=0;   // opened up: its torn edge, no stain round it hiding what's inside
                if(mask[i]<6&&priority[mark]>priority[mask[i]])mask[i]=mark;
                any=any||mark!=0;
            }
    }
    return any;
}

void woundVertex(Vertex &vertex,byte state,int tint) {
    if(state>=16) {   // a colour of its own: gore's cut faces (guts, bone), wet while fresh
        vertex.mode&=((1u<<MODE_WOUND_PALETTE_SHIFT)-1u)&~(MODE_GLOWING_EYES|MODE_WET_SPRITE);
        vertex.mode|=MODE_WOUND|((unsigned)state<<MODE_WOUND_PALETTE_SHIFT);
        if(settings.bloodShine&&!dryColour[state])vertex.mode|=MODE_WET_SPRITE;
        return;
    }
    if(state<2||state>5)return;
    const auto &shades=bloodPalette(tint);
    if(shades.empty())return;
    static const float shade[6]={0,0,0.02f,0.45f,0.82f,0.22f};
    byte color=shades[(size_t)std::lround(shade[state]*(shades.size()-1))];
    vertex.mode&=((1u<<MODE_WOUND_PALETTE_SHIFT)-1u)&~(MODE_GLOWING_EYES|MODE_WET_SPRITE);
    vertex.mode|=MODE_WOUND|((unsigned)color<<MODE_WOUND_PALETTE_SHIFT);
    if(state==5)vertex.mode|=MODE_WOUND_STAIN;
    else if(settings.bloodShine&&(state==3||state==4))vertex.mode|=MODE_WET_SPRITE;
}

// One texel row cut by the mask: runs of body texels, holes skipped, rim runs
// painted with the wound material. corner(edge, bottom) gives the vertex at the
// texture column edge (0..width) on the row's top or bottom; flip says the
// texture runs right to left on screen.
void maskedRow(std::vector<Vertex> &out,const std::vector<byte> &mask,int width,int r,bool flip,
               const std::function<Vertex(int,bool)> &corner,int tint) {
    int c=0;
    while(c<width) {
        byte state=mask[(size_t)r*width+c];
        int end=c+1;
        while(end<width&&mask[(size_t)r*width+end]==state)++end;
        if(state!=1) {
            int a=flip?end:c,b=flip?c:end;    // left to right on screen
            Vertex v[4]={corner(a,false),corner(b,false),corner(b,true),corner(a,true)};
            if(state>=2)for(Vertex &vertex:v)woundVertex(vertex,state,tint);
            quad(out,v[0],v[1],v[2],v[3]);
        }
        c=end;
    }
}
static bool rowWounded(const std::vector<byte> &mask,int width,int r) {
    for(int c=0;c<width;++c)if(mask[(size_t)r*width+c])return true;
    return false;
}

void woundedQuads(std::vector<Vertex> &out,const Vertex &base,const Image &image,const std::vector<byte> &mask,
                  float x,float y,float top,float rx,float ry,bool flip,float rowHeight,int tint) {
    int w=image.width,h=image.height;
    float left=-(float)image.left,right=left+w;
    auto at=[&](int edge,float z,float v) {
        float across=flip?right-edge:left+edge;
        Vertex vertex=base;vertex.x=x+rx*across;vertex.y=y+ry*across;vertex.z=z;vertex.u=(float)edge;vertex.v=v;
        return vertex;
    };
    // Unwounded rows go out as whole blocks, wounded ones as runs.
    int block=-1;
    auto flush=[&](int end) {
        if(block<0)return;
        int a=flip?w:0,b=flip?0:w;
        quad(out,at(a,top-block*rowHeight,(float)block),at(b,top-block*rowHeight,(float)block),
             at(b,top-end*rowHeight,(float)end),at(a,top-end*rowHeight,(float)end));
        block=-1;
    };
    for(int r=0;r<h;++r) {
        if(!rowWounded(mask,w,r)) {if(block<0)block=r;continue;}
        flush(r);
        maskedRow(out,mask,w,r,flip,[&](int edge,bool bottom){return at(edge,top-(r+(bottom?1:0))*rowHeight,(float)(r+(bottom?1:0)));},tint);
    }
    flush(h);
}
} // namespace doom3d
