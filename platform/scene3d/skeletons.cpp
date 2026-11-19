/* Gore's implied skeletons (PLAN-skin-suit.md 2-4): one per drawing of each
   monster the game profile gives a skeleton kind, so a wound is kept as "this
   bone, this far along, this far across" and found again on whatever drawing
   shows the monster next. Per drawing (sprite lump as stored), in order: the
   hand table (skeletons_stock.h), else the fitter (skeleton_fit.cpp) from the
   monster's ragdoll skeleton, the pain drawing first at each view, then every
   other walking, attacking and pain drawing from it, and from every other
   monster's hand-placed drawing at the same view. A drawing the fit leaves
   mostly uncovered gets none (its wounds fall back to wounds.cpp's patch
   matching). Kept by lump from level to level: the art doesn't change. Test
   switches: -skeletondump <file> writes every drawing's joints (for
   tests/skeleton_tool.py), -skeletonshow paints the bones over the monsters, -skeletonfitonly leaves
   the hand tables out (the fitter alone: what a monster with no hand rig gets). */
#include "internal.h"
#include "../skeletons_stock.h"
#include "../skeletons3d_stock.h"

namespace doom3d {
static std::unordered_map<int,SpriteSkeleton> byLump;
static std::unordered_map<int,bool> typesDone;
static std::unordered_map<const Image*,int> lumpOfImage;
constexpr float fitNeeded=0.8f;   // the share of a drawing's texels within reach of a bone

const SpriteSkeleton *spriteSkeleton(const Image &image) {
    auto lump=lumpOfImage.find(&image);
    if(lump==lumpOfImage.end())return nullptr;
    auto found=byLump.find(lump->second);
    if(found==byLump.end()||(!found->second.hand&&found->second.fit<fitNeeded))return nullptr;
    return &found->second;
}

// The bone's way, its length, and its right (always toward the drawing's right, whichever way it points).
static void frame(const SpriteSkeleton &s,int bone,const float *&a,float &dx,float &dy,float &l,float &nx,float &ny) {
    a=s.joint[skeletonBoneEnds[bone][0]];const float *b=s.joint[skeletonBoneEnds[bone][1]];
    dx=b[0]-a[0];dy=b[1]-a[1];l=std::max(1e-3f,std::hypot(dx,dy));
    nx=dy/l;ny=-dx/l;
    if(nx<0||(nx==0&&ny<0)) {nx=-nx;ny=-ny;}
}
void skeletonPlace(const SpriteSkeleton &s,int bone,float u,float h,float &along,float &across) {
    const float *a;float dx,dy,l,nx,ny;frame(s,bone,a,dx,dy,l,nx,ny);
    along=std::clamp(((u-a[0])*dx+(h-a[1])*dy)/(l*l),0.0f,1.0f);
    across=(u-a[0]-dx*along)*nx+(h-a[1]-dy*along)*ny;
}
void skeletonPoint(const SpriteSkeleton &s,int bone,float along,float across,float &u,float &h) {
    const float *a;float dx,dy,l,nx,ny;frame(s,bone,a,dx,dy,l,nx,ny);
    u=a[0]+dx*along+nx*across;h=a[1]+dy*along+ny*across;
}

static void dumpSkeleton(int lump,const SpriteSkeleton &s,const Image &image) {
    int arg=M_CheckParm((char*)"-skeletondump");
    if(!arg||arg+1>=myargc)return;
    static FILE *out=nullptr;
    if(!out)out=fopen(myargv[arg+1],"w");
    if(!out)return;
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    unsigned hash=2166136261u;
    for(byte p:image.pixels)hash=(hash^p)*16777619u;
    fprintf(out,"%s %d %d %u %.0f %.3f %d",name,image.width,image.height,hash,s.yaw*180/doomPi,s.fit,(int)s.hand);
    for(int j=0;j<SKELETON_JOINTS;++j)fprintf(out," %.2f %.2f",s.joint[j][0],s.joint[j][1]);
    for(int b=0;b<SKELETON_BONES;++b)fprintf(out," %d",(int)s.has[b]);
    fprintf(out,"\n");fflush(out);
}

// Whether a stock drawing's sprite (its name's first four letters) is a monster's whose hand rig is finished
// (Game_SkeletonFinished): only those start the fitter for other monsters.
static bool finishedSprite(const char *name) {
    static std::vector<std::string> finished;static bool made=false;
    if(!made) {
        made=true;
        for(int type=0;type<NUMMOBJTYPES;++type) {
            if(!Game_SkeletonFinished(type)||mobjinfo[type].spawnstate<=0)continue;
            int sprite=states[mobjinfo[type].spawnstate].sprite;
            if(sprite>=0&&sprite<numsprites)finished.push_back(std::string(sprnames[sprite],4));
        }
    }
    for(const std::string &f:finished)if(!strncmp(name,f.c_str(),4))return true;
    return false;
}

// One drawing: the joints placed by hand in it (skeletons_stock.h), exactly; the frame's 3D rig merged from
// all its drawings (skeletons3d_stock.h) gives their depth, or where none were placed in this drawing, the
// joints too, seen from its side. Else fitted from the start given (and from the second start, when there
// is one, keeping the better).
static void skeletonOf(const SkeletonReference &ref,int lump,float yaw,const SpriteSkeleton *start,const SpriteSkeleton *other) {
    if(byLump.count(lump))return;
    const Image &image=lumpImage(lump,false);
    if(image.pixels.empty())return;
    char name[9];memcpy(name,lumpinfo[lump].name,8);name[8]=0;
    SpriteSkeleton s,placed;
    bool placedByHand=false;
    const doom_rig3d_view *view=nullptr;
    static const bool fitOnly=M_CheckParm((char*)"-skeletonfitonly")!=0;
    // Known to have none (skeletons_stock.h): no fitting, no skeleton (its wounds: wounds.cpp's patch matching).
    if(!fitOnly&&doom_stock_skeleton_none_for(name,image.width,image.height,image.pixels.data())) {
        s.yaw=yaw;s.fit=0;lumpOfImage[&image]=lump;byLump[lump]=std::move(s);
        return;
    }
    if(fitOnly) {}
    else if(const doom_stock_rig3d *rig=doom_stock_rig3d_for(name,image.width,image.height,image.pixels.data(),&view)) {
        // x toward the body's left, y toward its back: the drawing's right is x cos + y sin, the eye x sin - y cos away.
        float a=view->yaw*doomPi/180,c=std::cos(a),sn=std::sin(a);
        for(int j=0;j<SKELETON_JOINTS;++j) {
            const float *p=rig->joint[j];
            placed.joint[j][0]=view->scale*(p[0]*c+p[1]*sn)+view->du;placed.joint[j][1]=view->scale*p[2]+view->dh;
            placed.depth[j]=p[0]*sn-p[1]*c;
        }
        placed.deep=placedByHand=true;
    }
    if(fitOnly) {}
    else if(const doom_stock_skeleton *hand=doom_stock_skeleton_for(name,image.width,image.height,image.pixels.data())) {
        memcpy(placed.joint,hand->joint,sizeof placed.joint);
        placedByHand=true;
    }
    if(placedByHand) {
        fitSkeleton(ref,image,std::cos(yaw),&placed,s,false);
        s.hand=true;
    } else {
        fitSkeleton(ref,image,std::cos(yaw),start,s);
        if(other) {
            SpriteSkeleton again;fitSkeleton(ref,image,std::cos(yaw),other,again);
            if(again.cost<s.cost)s=std::move(again);
        }
        // And from every drawing of another monster placed by hand at this rotation (skeletons_stock.h), scaled to
        // this one's height: its pose a start, this monster's proportions kept (the reference's), the best match
        // by the fit's own measure kept. Fitted so (the imp from the zombieman's hand rigs alone, and the other way
        // round) the joints came 8% to 14% nearer the hand rigs' on average, the worst tenth most.
        // (Each start weighed as it stands first, fitted only from the best four: a fit from every one took two
        // minutes for three monsters, and was no better.)
        std::vector<std::pair<float,SpriteSkeleton>> starts;
        // A drawing the game also shows mirrored (SPOSA4A6: rotation 4, and 6 flipped) stands in for the other
        // rotation flipped, its left and right swapped: a monster with eight drawings of its own (the chaingunner)
        // found no start at all for 6 to 8, the rigs all storing those as mirrors.
        for(const doom_stock_skeleton &t:doom_stock_skeleton_table) {
            if(!t.name||!strncmp(t.name,name,4))continue;
            bool mirrored=t.name[5]!=name[5];
            if(mirrored&&(strlen(t.name)<8||t.name[7]!=name[5]))continue;
            if(!finishedSprite(t.name))continue;
            SpriteSkeleton from;float k=(float)image.height/std::max(1,t.height);
            for(int j=0;j<SKELETON_JOINTS;++j) {
                int o=mirrored&&j>=SJ_SH0?(j-SJ_SH0)^1:j-SJ_SH0;   // (pairs from the shoulders on: right, left)
                int src=j<SJ_SH0?j:SJ_SH0+o;
                from.joint[j][0]=(mirrored?-1:1)*t.joint[src][0]*k;from.joint[j][1]=t.joint[src][1]*k;
            }
            SpriteSkeleton weighed;fitSkeleton(ref,image,std::cos(yaw),&from,weighed,false);
            starts.push_back({weighed.cost,from});
        }
        std::sort(starts.begin(),starts.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        // The four fits side by side, a thread each (the fitter shares nothing): one after the other, a monster
        // without a hand rig held the load for twenty seconds.
        size_t count=std::min<size_t>(starts.size(),4);
        std::vector<SpriteSkeleton> again(count);std::vector<std::thread> workers;
        for(size_t i=0;i<count;++i)workers.emplace_back([&,i]{fitSkeleton(ref,image,std::cos(yaw),&starts[i].second,again[i]);});
        for(std::thread &w:workers)w.join();
        for(SpriteSkeleton &fit:again)if(fit.cost<s.cost)s=std::move(fit);
    }
    s.yaw=yaw;
    buildBoneMap(s,image);
    if(M_CheckParm((char*)"-ragdolllog"))fprintf(stderr,"skeleton %s yaw %.0f fit %.2f%s\n",name,yaw*180/doomPi,s.fit,s.deep?" rig":s.hand?" hand":"");
    dumpSkeleton(lump,s,image);
    lumpOfImage[&image]=lump;
    byLump[lump]=std::move(s);
}

void prepareSkeletons() {
    for(const auto &entry:byLump)lumpOfImage[&lumpImage(entry.first,false)]=entry.first;   // the images loaded again
    // Only the monsters this map holds: fitting every kind with a skeleton at every load (a drawing without a
    // hand rig takes the fitter a quarter second or more) held "building the map" for twenty seconds. One
    // spawned later that wasn't fitted gets wounds.cpp's patch matching instead.
    std::vector<bool> present(NUMMOBJTYPES,false);
    for(int i=0;i<numsectors;++i)for(const mobj_t *thing=sectors[i].thinglist;thing;thing=thing->snext)
        if(thing->type>=0&&thing->type<NUMMOBJTYPES)present[thing->type]=true;
    for(int type=0;type<NUMMOBJTYPES;++type) {
        if(!present[type]||typesDone.count(type)||Game_Skeleton(type)!=GAME_SKELETON_HUMANOID)continue;
        typesDone[type]=true;
        const mobjinfo_t &info=mobjinfo[type];
        SkeletonReference ref;
        if(info.painstate<=0||!referenceSkeleton(type,ref))continue;
        const state_t &pain=states[info.painstate];
        if(pain.sprite<0||pain.sprite>=numsprites)continue;
        const auto &sprite=sprites[pain.sprite];int painFrame=pain.frame&FF_FRAMEMASK;
        if(painFrame>=sprite.numframes)continue;
        measureSkeletonRadii(ref,lumpImage(firstspritelump+sprite.spriteframes[painFrame].lump[0],false));
        // The pain drawing at each view from the reference, then each other drawing from the pain drawing at
        // its view and from the drawing before it in the sprite (the walk's previous step).
        std::vector<int> order={painFrame};
        for(int f=0;f<sprite.numframes;++f)if(f!=painFrame)order.push_back(f);
        int previous[8];for(int &p:previous)p=-1;
        for(int f:order) {
            const spriteframe_t &sf=sprite.spriteframes[f];
            if(!sf.rotate)continue;   // lying drawings, seen alike from every side: no standing skeleton
            for(int r=0;r<8;++r) {
                if(sf.flip[r])continue;   // a mirrored use: fitted as its own view
                int lump=firstspritelump+sf.lump[r];
                auto painAt=byLump.find(firstspritelump+sprite.spriteframes[painFrame].lump[r]);
                const SpriteSkeleton *start=f==painFrame||painAt==byLump.end()?nullptr:&painAt->second;
                auto before=previous[r]>=0?byLump.find(previous[r]):byLump.end();
                skeletonOf(ref,lump,r*doomPi/4,start,before!=byLump.end()&&&before->second!=start?&before->second:nullptr);
                previous[r]=lump;
            }
        }
    }
}
void skeletonsShutdown() {lumpOfImage.clear();}
} // namespace doom3d
