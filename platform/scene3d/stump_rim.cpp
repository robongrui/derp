/* A stump's torn end (PLAN-tear-fibres.md 2 and 2b): the card's cuts (a severed
   neck, the halves, an arm or a leg off, the kill's blow, chunks shot out of the
   corpse, a bite out of the skull) drawn as a tear, not a line. The doll's own
   cut tests (ragdoll_doll.h) still say what went; here the line between is moved
   in and out a few texels by seeded noise, the cut face is painted in rings from
   the cut in (a skin lip, bright meat, a dark core), skin flaps hang past it over
   the wound, and bone shows where the drawing's bone map (bone_map.cpp) has it,
   standing out past the cut as a splinter on one side of the break. Drawing only:
   a texel grid worked out whenever the card is drawn again. */
#include "stump_rim.h"

namespace doom3d {
// The kinds of cut, each with its own seed: how far its line moves either way (texels), and how deep its
// painted face reaches in from it. Chunks and a skull's bite are small: torn harder they'd lose their shape.
enum { STUMP_NECK,STUMP_HALF,STUMP_ARM0,STUMP_ARM1,STUMP_LEG0,STUMP_LEG1,STUMP_BLOW,STUMP_BITES,STUMP_SKULL,STUMP_CUTS };
struct CutRule { float reach;int depth; };
static const CutRule cutRules[STUMP_CUTS]={{2.5f,4},{3,4},{2.5f,3},{2.5f,3},{2.5f,3},{2.5f,3},{2,3},{1,2},{1,2}};
constexpr float rimNear=7;   // texels from a cut's own rim its rules reach: past that a lost texel is simply gone

static uint32_t seedOf(const Ragdoll &doll,int k) {return hash32(doll.goreSeed^(0x9e3779b9u*(uint32_t)(k+1)));}
// How far a cut's line moves in (toward the stump, positive) at a texel: lumps a few texels long and a
// texel's grain on them, never the same on two kills. The legs' doll takes the top's line the other way
// round, so the two torn ends fit.
static float cutOffset(const Ragdoll &doll,int k,int c,int r) {
    uint32_t s=seedOf(doll,k);
    float lumps=valueNoise((float)c,(float)r,3,s)*2-1,grain=hashUnit(c,r,s^0x2bu)*2-1;
    float off=cutRules[k].reach*std::clamp(1.3f*lumps*0.75f+0.25f*grain,-1.0f,1.0f);
    return doll.lowerHalf&&k==STUMP_HALF?-off:off;
}
// The bones a cut shows: its own limb's (a shoulder cut showing the ribs beside it read as a hole in the chest).
static bool boneShows(int k,int b) {
    switch(k) {
    case STUMP_NECK:return b==SB_SPINE||b==SB_HEAD;
    case STUMP_HALF:return b==SB_SPINE||b==SB_PELVIS||b==SB_THIGH0||b==SB_THIGH1;
    case STUMP_ARM0:case STUMP_ARM1:return b>=SB_UPPER0&&b<=SB_FORE1;
    case STUMP_LEG0:case STUMP_LEG1:return b==SB_PELVIS||b>=SB_THIGH0;
    default:return true;
    }
}
// The drawing the relief was raised from (as ragdoll.cpp's frameLump: the pain frame from the front).
static int frontLump(int state) {
    if(state<=0)return -1;
    const state_t &s=states[state];
    if(s.sprite<0||s.sprite>=numsprites)return -1;
    int frame=s.frame&FF_FRAMEMASK;
    if(frame>=sprites[s.sprite].numframes)return -1;
    return firstspritelump+sprites[s.sprite].spriteframes[frame].lump[0];
}
// Chamfer distances over the texel grid (1 a step, 1.4 diagonally), into the cells walk lets in only, from
// cells already at 0; label carries along whose they are. edge: off the grid counts as a source.
static void spread(int w,int h,const std::vector<byte> &walk,std::vector<float> &d,std::vector<int32_t> *label,bool edge) {
    auto relax=[&](int c,int r,int dc,int dr,float step) {
        int c2=c+dc,r2=r+dr;size_t i=(size_t)r*w+c;
        if(c2<0||r2<0||c2>=w||r2>=h) {if(edge)d[i]=std::min(d[i],step);return;}
        size_t j=(size_t)r2*w+c2;
        if(d[j]+step<d[i]) {d[i]=d[j]+step;if(label)(*label)[i]=(*label)[j];}
    };
    for(int round=0;round<2;++round) {   // twice: walls in the grid bend the shortest ways
        for(int r=0;r<h;++r)for(int c=0;c<w;++c)if(walk[(size_t)r*w+c]) {relax(c,r,-1,0,1);relax(c,r,0,-1,1);relax(c,r,-1,-1,1.4f);relax(c,r,1,-1,1.4f);}
        for(int r=h-1;r>=0;--r)for(int c=w-1;c>=0;--c)if(walk[(size_t)r*w+c]) {relax(c,r,1,0,1);relax(c,r,0,1,1);relax(c,r,1,1,1.4f);relax(c,r,-1,1,1.4f);}
    }
}

// Bones break rather than cut, and the long splinter stays with one side: the stump or the piece. A
// forearm's or a shin's two shafts never break the same way.
bool stumpKeepsSplinter(uint32_t goreSeed,int bone) {
    if(bone>=SKELETON_BONES)return !stumpKeepsSplinter(goreSeed,bone-SKELETON_BONES);
    return hashUnit(bone,0x51,goreSeed^0x3c6ef372u)<0.5f;
}
int stumpSplinterBone(const SpriteSkeleton &s,int c,int r) {
    if(c<0||r<0||c>=s.width||r>=s.height||s.owner.size()!=(size_t)s.width*s.height)return -1;
    int b=s.owner[(size_t)r*s.width+c];
    if(b==SB_FORE0||b==SB_FORE1||b==SB_SHIN0||b==SB_SHIN1) {
        float along,across;skeletonPlace(s,b,c-s.left+0.5f,s.top-r-0.5f,along,across);
        if(across>0)b+=SKELETON_BONES;   // the shaft toward the drawing's right
    }
    return b;
}

static void buildRim(const Ragdoll &doll,int tint,const byte rims[4],StumpRim &rim) {
    const Relief &body=*doll.body;
    const int w=body.width,h=body.height;const size_t n=(size_t)w*h;
    bool limbs=!doll.lowerHalf&&(doll.armOff[0]||doll.armOff[1]||doll.legOff[0]||doll.legOff[1]||doll.blow||!doll.bites.empty());
    rim.on=(doll.headless||doll.halved||doll.torn||limbs)&&body.frontFace.size()==n;
    if(!rim.on)return;
    rim.paint.assign(n,STUMP_KEEP);rim.colour.assign(n,0);rim.from.assign(n,-1);
    static std::vector<byte> present,lost,seed,rigid,stripped,zone,kept,extra;
    static std::vector<int32_t> kind,nearKind,from;
    static std::vector<float> fx,fz,nearD,toLost,toKept,toOut,edge;
    for(auto *v:{&present,&lost,&seed,&rigid,&stripped,&zone,&kept,&extra})v->assign(n,0);
    kind.assign(n,-1);fx.assign(n,0);fz.assign(n,0);
    // What the doll's cuts say of each texel, by its front face (its back and its walls go with it).
    for(size_t i=0;i<n;++i) {
        int32_t fi=body.frontFace[i];
        if(fi<0)continue;
        const ReliefFace &face=body.faces[fi];
        present[i]=1;
        V3 c{};float gun=0;
        for(int k=0;k<4;++k) {
            const ReliefCorner &corner=body.corners[face.corner[k]];c=c+corner.p*0.25f;
            for(int j=0;j<4;++j)if(corner.part[j]==PART_GUN)gun+=corner.w[j]*0.25f;
        }
        fx[i]=c.x;fz[i]=c.z;
        auto mark=[&](int k,int state) {   // state as the doll's tests: 0 gone, 1 the cut's rim, 2 neither
            if(state==0) {if(!lost[i]) {lost[i]=1;kind[i]=k;}}
            else if(state==1&&!lost[i]&&!seed[i]) {seed[i]=1;kind[i]=k;}
        };
        if(doll.headless) {
            float head=headShare(body,face);
            if(headLost(body,face,head,doll.goreSeed))mark(STUMP_NECK,0);
            else if(head>0.02f&&overNeck(body,face,1.5f))mark(STUMP_NECK,1);
        }
        if(doll.halved) {
            // The gun and the arms go with the top whatever the cut: their edge against the legs is no cut.
            if(gun+doll.armShare(face)>0.5f)rigid[i]=1;
            int part=doll.lowerHalf?doll.legsCut(face):doll.cut(face);
            mark(STUMP_HALF,part);
        }
        if(!doll.lowerHalf) {
            for(int s=0;s<2;++s)if(doll.armOff[s])mark(STUMP_ARM0+s,doll.armCut(face,s));
            for(int s=0;s<2;++s)if(doll.legOff[s])mark(STUMP_LEG0+s,doll.legCut(face,s));
            if(doll.blow) {
                int blown=goreBlowState(doll,i);   // 1 gone, 2 its torn edge, 3 stripped to the bone
                if(blown==3)stripped[i]=1;
                mark(STUMP_BLOW,blown==1?0:blown>=2?1:2);
            }
            if(!doll.bites.empty())mark(STUMP_BITES,doll.bitten(face));
        }
        if(doll.torn&&!doll.headless)mark(STUMP_SKULL,doll.bite(face));
    }
    // How far each texel lies from a cut's own rim, through the body (never across a gap in the drawing),
    // and whose rim it is: only there is the line torn. A lost limb's long edge against what it lay over
    // (an arm hanging across the chest) is the drawing's outline, not a cut.
    nearD.assign(n,1e4f);nearKind=kind;
    for(size_t i=0;i<n;++i) {
        if(!present[i])continue;
        if(seed[i]&&!lost[i]) {nearD[i]=0;continue;}
        if(!lost[i])continue;
        int c=(int)(i%w),r=(int)(i/w);
        for(auto [dc,dr]:{std::pair{1,0},std::pair{-1,0},std::pair{0,1},std::pair{0,-1}}) {
            int c2=c+dc,r2=r+dr;
            if(c2<0||r2<0||c2>=w||r2>=h)continue;
            size_t j=(size_t)r2*w+c2;
            if(present[j]&&seed[j]&&!lost[j]) {nearD[i]=0;break;}
        }
    }
    for(size_t i=0;i<n;++i)if(nearD[i]>0&&!lost[i])nearKind[i]=-1;
    spread(w,h,present,nearD,&nearKind,false);
    for(size_t i=0;i<n;++i)zone[i]=present[i]&&nearD[i]<=rimNear&&(lost[i]?kind[i]:nearKind[i])>=0;
    // Within that: each stump texel's distance to what went, each lost texel's to the stump (and which
    // stump texel is nearest: a flap's colour), and every texel's to the drawing's outline.
    static std::vector<byte> walk;walk.assign(n,0);
    toLost.assign(n,1e4f);
    for(size_t i=0;i<n;++i) {if(zone[i]&&lost[i])toLost[i]=0;walk[i]=zone[i]&&!lost[i];}
    spread(w,h,walk,toLost,nullptr,false);
    toKept.assign(n,1e4f);from.assign(n,-1);
    for(size_t i=0;i<n;++i) {if(zone[i]&&!lost[i]) {toKept[i]=0;from[i]=(int32_t)i;}walk[i]=zone[i]&&lost[i];}
    spread(w,h,walk,toKept,&from,false);
    toOut.assign(n,1e4f);
    for(size_t i=0;i<n;++i)if(!present[i])toOut[i]=0;
    spread(w,h,present,toOut,nullptr,true);

    int lump=frontLump(doll.state);
    const SpriteSkeleton *skel=lump>=0?spriteSkeleton(lumpImage(lump,false)):nullptr;
    if(skel&&(skel->boneMap.size()!=n||skel->owner.size()!=n||skel->width!=w))skel=nullptr;
    auto boneCode=[&](size_t i,int k) {
        if(!skel||k<0)return (int)BONE_NONE;
        int b=skel->owner[i];
        return b>=0&&boneShows(k,b)?(int)skel->boneMap[i]:(int)BONE_NONE;
    };
    auto isBone=[](int code){return code==BONE_SOLID||code==BONE_EDGE;};
    // The line torn: each texel's signed distance past it (positive on the stump).
    edge.assign(n,-1e4f);
    for(size_t i=0;i<n;++i) {
        if(!zone[i])continue;
        int k=lost[i]?kind[i]:nearKind[i];
        float s=lost[i]?0.5f-toKept[i]:toLost[i]-0.5f;
        if(!rigid[i])s-=cutOffset(doll,k,(int)(i%w),(int)(i/w));
        // Cut flesh pulls back, bone doesn't: the noise never takes a bone texel off the stump.
        if(!lost[i]&&s<=0&&(stripped[i]||isBone(boneCode(i,k))))s=0.01f;
        edge[i]=s;kept[i]=s>0;
    }
    // Skin flaps: two or three spots on each cut where the lip hangs a texel or two past it over the wound.
    static std::vector<std::pair<float,size_t>> spots[STUMP_CUTS];
    for(auto &list:spots)list.clear();
    for(size_t i=0;i<n;++i)if(zone[i]&&kept[i]&&!rigid[i]&&edge[i]<1&&toOut[i]>1.5f) {
        int k=lost[i]?kind[i]:nearKind[i];
        spots[k].push_back({hashUnit((int)(i%w),(int)(i/w),seedOf(doll,k)^0x1f1au),i});
    }
    for(int k=0;k<STUMP_CUTS;++k) {
        auto &list=spots[k];
        size_t count=std::min(list.size(),(size_t)(2+(hashUnit(k,7,seedOf(doll,k))<0.5f)));
        std::partial_sort(list.begin(),list.begin()+count,list.end());
        for(size_t m=0;m<count;++m) {
            int c=(int)(list[m].second%w),r=(int)(list[m].second/w);
            int reach=1+(hashUnit(c,r,seedOf(doll,k)^0x2eu)<0.5f);
            for(int dr=-reach;dr<=reach;++dr)for(int dc=-reach;dc<=reach;++dc) {
                if(std::abs(dc)==reach&&std::abs(dr)==reach)continue;   // ragged, not square
                int c2=c+dc,r2=r+dr;
                if(c2<0||r2<0||c2>=w||r2>=h)continue;
                size_t j=(size_t)r2*w+c2;
                if(zone[j]&&!kept[j]&&!rigid[j]&&edge[j]>-reach)extra[j]=1;
            }
        }
    }
    // Bone sticks out: past the cut, as far as one to three texels a column, where this side keeps the
    // long splinter of that bone (the piece keeps it otherwise; the legs' doll is the top's piece).
    // (The long bones and the spine break so: the skull and the pelvis only show where they were cut.)
    auto breaks=[](int key){return key>=0&&key%SKELETON_BONES!=SB_HEAD&&key%SKELETON_BONES!=SB_PELVIS;};
    auto keeps=[&](int key){return stumpKeepsSplinter(doll.goreSeed,key)!=doll.lowerHalf;};
    if(skel)for(size_t i=0;i<n;++i) {
        if(!zone[i]||kept[i]||rigid[i]||!isBone(boneCode(i,kind[i])))continue;
        int c=(int)(i%w),r=(int)(i/w),key=stumpSplinterBone(*skel,c,r);
        if(!breaks(key)||!keeps(key))continue;
        float most=1+std::floor(2.99f*valueNoise((float)c,(float)r,2,seedOf(doll,kind[i])^((uint32_t)key*0x85ebca6bu)));
        if(edge[i]>-most)extra[i]=2;
    }
    // Where the piece took the splinter, the broken end here shows the marrow: one dark texel in each bone's end.
    float marrowAt[2*SKELETON_BONES];size_t marrow[2*SKELETON_BONES];
    for(float &v:marrowAt)v=2;
    if(skel)for(size_t i=0;i<n;++i) {
        if(!zone[i]||!kept[i]||rigid[i]||edge[i]>=1||toOut[i]<=1.5f)continue;
        int k=lost[i]?kind[i]:nearKind[i];
        if(!isBone(boneCode(i,k)))continue;
        int c=(int)(i%w),r=(int)(i/w),key=stumpSplinterBone(*skel,c,r);
        if(!breaks(key)||keeps(key))continue;
        float v=hashUnit(c,r,doll.goreSeed^0x6d2bu);
        if(v<marrowAt[key]) {marrowAt[key]=v;marrow[key]=i;}
    }

    byte dark=bloodRemap(rims[0],tint);
    auto paint=[&](size_t i,byte colour) {rim.paint[i]=STUMP_COLOUR;rim.colour[i]=colour;};
    auto flap=[&](size_t i) {if(from[i]>=0) {rim.paint[i]=STUMP_FLAP;rim.from[i]=from[i];}};
    for(size_t i=0;i<n;++i) {
        if(!present[i])continue;
        if(!zone[i]) {
            if(lost[i])rim.paint[i]=STUMP_GONE;
            else if(stripped[i])paint(i,boneMapColour(BONE_SOLID));
            continue;
        }
        int k=lost[i]?kind[i]:nearKind[i],c=(int)(i%w),r=(int)(i/w),code=boneCode(i,k);
        if(extra[i]==2) {paint(i,boneMapColour(code));continue;}
        if(extra[i]==1) {flap(i);continue;}
        if(!kept[i]) {rim.paint[i]=STUMP_GONE;continue;}
        if(rigid[i])continue;
        // Rings from the cut in, and round the limb from its outline in: as a stump's end looks, a skin
        // lip, bright meat, a dark core, the bright meat again where it meets the skin.
        int depth=(int)edge[i],most=cutRules[k].depth+(valueNoise((float)c,(float)r,2.5f,seedOf(doll,k)^0x5au)>0.5f?1:0);
        if(depth>=most) {
            if(lost[i])flap(i);   // the line moved out past the lost part's texels: the stump's skin there
            else if(stripped[i])paint(i,boneMapColour(BONE_SOLID));
            continue;
        }
        int ring=std::min(depth,(int)(toOut[i]-1));
        if(depth==most-1&&ring>=2)ring=1;
        float pick=hashUnit(c,r,seedOf(doll,k)^0x7fu);
        bool inside=toOut[i]>1.5f;   // bone sits inside the limb, never on its outline
        if(stripped[i]||(inside&&isBone(code)))paint(i,boneMapColour(isBone(code)?code:BONE_SOLID));
        else if(inside&&code==BONE_HOLE)paint(i,dark);   // marrow, and the gaps of the rib cage
        else if(ring==0) {if(pick>=0.8f)paint(i,bloodRemap(rims[1],tint));else if(lost[i])flap(i);}
        else if(ring==1) {
            byte meat=doll.guts&&pick<0.45f?gutsColour(*doll.guts,c,r,seedOf(doll,k)):0;
            paint(i,meat>=16?meat:bloodRemap(rims[pick<0.7f?1:2],tint));
        }
        else paint(i,bloodRemap(rims[pick<0.6f?0:3],tint));
        // A skull bitten on a monster with no rig: its broken edge bone here and there, as the map would give.
        if(!skel&&k==STUMP_SKULL&&ring<=1&&pick>0.65f)paint(i,boneMapColour(BONE_SOLID));
    }
    for(int key=0;key<2*SKELETON_BONES;++key)if(marrowAt[key]<2)paint(marrow[key],dark);
    // No rig, no bone map: a dot of bone at the cut part's joint, two texels square so it reads at Doom's size.
    if(!skel) {
        std::vector<std::pair<int,int>> joints;   // kind, the doll's point
        if(doll.headless)joints.push_back({STUMP_NECK,P_NECK});
        if(doll.halved) {
            if(doll.lowerHalf) {joints.push_back({STUMP_HALF,P_HIP_L});joints.push_back({STUMP_HALF,P_HIP_R});}
            else joints.push_back({STUMP_HALF,P_SPINE});
        }
        if(!doll.lowerHalf)for(int s=0;s<2;++s) {
            if(doll.armOff[s])joints.push_back({STUMP_ARM0+s,s?P_SH_R:P_SH_L});
            if(doll.legOff[s])joints.push_back({STUMP_LEG0+s,doll.legOff[s]==2?(s?P_HIP_R:P_HIP_L):(s?P_KNEE_R:P_KNEE_L)});
        }
        for(auto [k,point]:joints) {
            V3 at=body.rest[point];float best=1e9f;size_t found=n;
            for(size_t i=0;i<n;++i) {
                if(!zone[i]||!kept[i]||rigid[i]||edge[i]>=2||toOut[i]<=1.5f||(lost[i]?kind[i]:nearKind[i])!=k)continue;
                float d=(fx[i]-at.x)*(fx[i]-at.x)+(fz[i]-at.z)*(fz[i]-at.z);
                if(d<best) {best=d;found=i;}
            }
            if(found==n)continue;
            int c=(int)(found%w),r=(int)(found/w);
            for(int dr=0;dr<2;++dr)for(int dc=0;dc<2;++dc) {
                int c2=c+dc,r2=r+dr;
                if(c2>=w||r2>=h)continue;
                size_t j=(size_t)r2*w+c2;
                if(zone[j]&&kept[j]&&!rigid[j])paint(j,boneMapColour(BONE_SOLID));
            }
        }
    }
}
// The rim doesn't change with the pose, only with the cuts: worked out again only when they change. A falling
// body is drawn again every tic (every frame in slow motion), and the grid's distances cost more than the picture.
struct KeptRim { uint64_t cuts=0;StumpRim rim; };
static std::unordered_map<const Ragdoll*,KeptRim> keptRims;
static uint64_t cutsOf(const Ragdoll &doll,int tint) {
    uint64_t h=0xcbf29ce484222325ull;
    auto mix=[&](uint64_t v){h=(h^v)*0x100000001b3ull;};
    auto bits=[](float f){uint32_t u;memcpy(&u,&f,4);return (uint64_t)u;};
    mix((uintptr_t)doll.body);mix(doll.goreSeed);mix((uint64_t)doll.cutSerial);mix((uint64_t)tint);mix((uintptr_t)doll.guts);
    mix((uint64_t)doll.headless|(uint64_t)doll.torn<<1|(uint64_t)doll.halved<<2|(uint64_t)doll.lowerHalf<<3|(uint64_t)doll.armOff[0]<<4|
        (uint64_t)doll.armOff[1]<<5|(uint64_t)doll.legBlown[0]<<6|(uint64_t)doll.legBlown[1]<<7);
    mix((uint64_t)doll.legOff[0]|(uint64_t)doll.legOff[1]<<8);mix((uint64_t)doll.state);
    mix(bits(doll.cutZ));mix(bits(doll.cutSlant));mix(bits(doll.biteX));mix(bits(doll.biteZ));mix(bits(doll.biteR));
    mix((uintptr_t)doll.blow.get());mix(doll.bites.size());
    for(const auto &b:doll.bites) {mix(bits(b.x));mix(bits(b.z));mix(bits(b.r));}
    return h;
}
const StumpRim &stumpRim(const Ragdoll &doll,int tint,const byte rims[4]) {
    uint64_t cuts=cutsOf(doll,tint);
    auto found=keptRims.find(&doll);
    if(found!=keptRims.end()&&found->second.cuts==cuts)return found->second.rim;
    if(found==keptRims.end()) {
        if(keptRims.size()>=64)keptRims.clear();   // dolls dropped without a word: start over now and then
        found=keptRims.emplace(&doll,KeptRim{}).first;
    }
    found->second.cuts=cuts;
    buildRim(doll,tint,rims,found->second.rim);
    return found->second.rim;
}
void stumpRimDrop(const Ragdoll &doll) {keptRims.erase(&doll);}
byte stumpPieceBone(const Ragdoll &doll,size_t texel) {
    int lump=frontLump(doll.state);
    const SpriteSkeleton *skel=lump>=0?spriteSkeleton(lumpImage(lump,false)):nullptr;
    if(!skel||skel->width<=0||texel>=skel->boneMap.size()||texel>=skel->owner.size())return 0;
    int code=skel->boneMap[texel];
    if(code==BONE_HOLE)return 2;
    if(code!=BONE_SOLID&&code!=BONE_EDGE)return 0;
    int w=skel->width,bone=stumpSplinterBone(*skel,(int)(texel%w),(int)(texel/w));
    // The stump kept the long splinter: this end broke off short, the marrow showing in it here and there.
    if(bone>=0&&stumpKeepsSplinter(doll.goreSeed,bone)&&hashUnit((int)texel,9,doll.goreSeed)<0.4f)return 2;
    return 1;
}
}
