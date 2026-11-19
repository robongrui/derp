/* Gore's head burst (PLAN-gore.md N): a head blown apart as a picture trick, not
   flight. The head as the body last showed it is cut into uneven pieces (a
   weighted Voronoi split of its texels, the borders wobbled so the cracks
   tear), which spread out from where the shot struck it, easing to a stop and
   sagging a little, over a lumpy cloud of blood, then each dissolves on a
   dither of its own. A small picture of its own, facing the eye, riding where
   the head was on the falling body, redrawn each tic for about a third of a second. Every number
   comes from the kill's own seed, never the game's: no two heads burst alike. */
#include "internal.h"

namespace doom3d {
constexpr int burstKeyBase=-0x60000000;   // image keys clear of walls, lumps, blood, puddles, cards and speech
static int burstSerial=0;
// The budget (PLAN-gore.md L): at most this many bursting at once; past it a head still pops in blood
// (spray.cpp) but bursts as no picture. Each is a 64-pixel picture redrawn every tic for a third of a
// second, so a crowd blown apart at once stays cheap.
constexpr int maxHeadBursts=12;
static int bursting=0;

struct BurstPiece {
    std::vector<int> x,z;                 // its texels, picture units from the head's middle
    std::vector<byte> colour;
    std::vector<bool> torn;               // on a crack or the neck's cut: goes red as it opens
    float cx=0,cz=0,reachX=0,reachZ=0;    // its middle, and where it eases out to from there
    int turnAt=-1,turn=0;                 // the tic it turns, by 90 degrees that many times (4: flipped)
    float fadeAt=0,fadeLen=1;int ditherX=0,ditherY=0;
};
struct HeadBurst {
    float hitX=0,hitZ=0,shotX=0,shotZ=1,tau=3,sag=0.05f;
    float cloudReach=12,cloudTau=2.5f,cloudStretch=1.3f,cloudFadeAt=4,cloudFadeLen=6;
    uint32_t seed=0;int tint=BLOOD_RED;bool bleeds=true;const GutsPatch *guts=nullptr;
    std::vector<BurstPiece> pieces;
    int key=0,drawnTic=-1,end=0;
    int canvas=64;                        // the picture, a pixel a unit; the head's middle at its middle
    bool counted=true;                    // still counted as bursting (until it ends or is freed)
    HeadBurst() {++bursting;}
    ~HeadBurst() {if(counted)--bursting;}
};
int headBurstsLive() {return bursting;}

static float valueNoise(float x,float z,uint32_t seed) {   // smooth lumps about a cell wide
    int ix=(int)std::floor(x),iz=(int)std::floor(z);
    float fx=x-ix,fz=z-iz;fx=fx*fx*(3-2*fx);fz=fz*fz*(3-2*fz);
    float a=hashUnit(ix,iz,seed),b=hashUnit(ix+1,iz,seed),c=hashUnit(ix,iz+1,seed),d=hashUnit(ix+1,iz+1,seed);
    return (a+(b-a)*fx)+((c+(d-c)*fx)-(a+(b-a)*fx))*fz;
}
static const byte bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
static byte nearest(const byte *palette,int r,int g,int b) {
    int best=0,bestDistance=INT_MAX;
    for(int i=0;i<256;++i) {
        int dr=palette[3*i]-r,dg=palette[3*i+1]-g,db=palette[3*i+2]-b,d=dr*dr+dg*dg+db*db;
        if(d<bestDistance) {bestDistance=d;best=i;}
    }
    return (byte)best;
}

std::vector<int> burstCut(const std::vector<HeadTexel> &texels,int count,uint32_t seed,std::vector<bool> &torn,bool cutBelow) {
    uint32_t s=seed^0x5bd1e995u;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    std::vector<int> seedOf(texels.size(),0);torn.assign(texels.size(),false);
    if(texels.empty()||count<1)return seedOf;
    // Each seed with a weight (uneven pieces), the distance to each wobbled per texel and seed (ragged
    // cracks); the wobble a texel or two, whatever the picture's size.
    std::vector<float> weight(count);std::vector<size_t> seeds(count);
    for(int k=0;k<count;++k) {seeds[k]=(size_t)(rnd()*texels.size())%texels.size();weight[k]=0.6f+0.8f*rnd();}
    for(size_t i=0;i<texels.size();++i) {
        float best=1e9f;
        for(int k=0;k<count;++k) {
            const HeadTexel &q=texels[seeds[k]];
            float d=std::hypot((float)(texels[i].x-q.x),(float)(texels[i].z-q.z))/weight[k]+1.6f*hashUnit(texels[i].x*31+k,texels[i].z,seed^0x2f6b1a3du);
            if(d<best) {best=d;seedOf[i]=k;}
        }
    }
    // The texels on a grid, to find who borders whom.
    std::map<std::pair<int,int>,size_t> at;
    for(size_t i=0;i<texels.size();++i)at[{texels[i].x,texels[i].z}]=i;
    static const int ways[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(size_t i=0;i<texels.size();++i)
        for(const auto &w:ways) {
            auto found=at.find({texels[i].x+w[0],texels[i].z+w[1]});
            if(found!=at.end()?seedOf[found->second]!=seedOf[i]:cutBelow&&w[1]<0)torn[i]=true;   // a crack, or the neck's cut below
        }
    return seedOf;
}

std::shared_ptr<HeadBurst> startHeadBurst(const std::vector<HeadTexel> &head,const HeadBurstStart &start) {
    if(head.size()<4||bursting>=maxHeadBursts)return nullptr;
    auto burst=std::make_shared<HeadBurst>();
    HeadBurst &b=*burst;
    b.seed=start.seed;b.tint=start.tint;b.bleeds=start.bleeds;b.guts=start.guts;b.canvas=start.canvas;
    uint32_t s=start.seed;
    auto rnd=[&] {s=hash32(s+0x9e3779b9u);return (float)(s>>8)/16777216.0f;};
    b.hitX=std::clamp(start.hitX,-start.hitMost,start.hitMost);b.hitZ=std::clamp(start.hitZ,-start.hitMost,start.hitMost);
    float across=std::clamp(start.shotAcross,-1.0f,1.0f);
    float sl=std::hypot(across,0.3f);b.shotX=across/sl;b.shotZ=0.3f/sl;
    b.tau=2+2*rnd();b.sag=0.03f+0.04f*rnd();
    // The cut: 5-11 seeds on the head's own texels (or as already cut).
    std::vector<int> seedOf;std::vector<bool> tornOf;int count=0;
    if(start.pieceOf&&start.torn) {
        seedOf=*start.pieceOf;tornOf=*start.torn;
        for(int k:seedOf)count=std::max(count,k+1);
    } else {
        count=std::clamp(5+(int)(rnd()*7),3,std::max(3,(int)head.size()/4));
        seedOf=burstCut(head,count,start.seed,tornOf,true);
    }
    b.pieces.resize(count);
    for(size_t i=0;i<head.size();++i) {
        BurstPiece &piece=b.pieces[seedOf[i]];
        const HeadTexel &t=head[i];
        piece.x.push_back(t.x);piece.z.push_back(t.z);piece.colour.push_back(t.colour);piece.torn.push_back(tornOf[i]);
    }
    b.pieces.erase(std::remove_if(b.pieces.begin(),b.pieces.end(),[](const BurstPiece &p){return p.x.empty();}),b.pieces.end());
    float headReach=0;
    for(const HeadTexel &t:head)headReach=std::max(headReach,std::hypot((float)t.x,(float)t.z));
    for(BurstPiece &piece:b.pieces) {
        for(size_t i=0;i<piece.x.size();++i) {piece.cx+=piece.x[i]+0.5f;piece.cz+=piece.z[i]+0.5f;}
        piece.cx/=piece.x.size();piece.cz/=piece.x.size();
        // Away from where it was struck, turned up to 20 degrees either way; small bits further.
        float wx=piece.cx-b.hitX,wz=piece.cz-b.hitZ,wl=std::hypot(wx,wz);
        float a=wl>0.5f?std::atan2(wz,wx):rnd()*2*doomPi;
        a+=(rnd()*2-1)*20*doomPi/180;
        float reach=std::clamp(start.blow*40*start.reachScale/std::sqrt((float)piece.x.size())*(0.7f+0.6f*rnd()),3.0f,start.reachMost);
        float rx=std::cos(a)*reach,rz=std::sin(a)*reach,along=rx*b.shotX+rz*b.shotZ;
        piece.reachX=rx+b.shotX*along*0.5f*std::abs(across);piece.reachZ=rz+b.shotZ*along*0.5f*std::abs(across);
        if(rnd()<0.6f) {piece.turnAt=2+(int)(rnd()*4);piece.turn=1+(int)(rnd()*4);}
        piece.fadeAt=3+4*rnd();piece.fadeLen=2+3*rnd();
        piece.ditherX=(int)(rnd()*4);piece.ditherY=(int)(rnd()*4);
        b.end=std::max(b.end,(int)std::ceil(piece.fadeAt+piece.fadeLen)+1);
    }
    b.cloudReach=headReach*(0.7f+0.3f*rnd());b.cloudTau=1.5f+rnd();b.cloudStretch=1+0.7f*std::abs(across)*(0.6f+0.4f*rnd());
    b.cloudFadeAt=1.5f+2*rnd();b.cloudFadeLen=3+2*rnd();
    b.end=std::max(b.end,(int)std::ceil(b.cloudFadeAt+b.cloudFadeLen)+1);
    return burst;
}

void dropHeadBurst(HeadBurst &b) {
    if(b.counted) {b.counted=false;--bursting;}
    if(!b.key)return;
    images.erase(b.key);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(b.key);
    shadowBatches.erase(-1-b.key);decalBatches.erase(-1-b.key);   // its shadows, under the inverse key (a stale one crashed the frame)
    b.key=0;
}

bool headBurstCard(HeadBurst &b,int tic,float x,float y,float z,RagdollCard &card) {
    if(tic>=b.end) {dropHeadBurst(b);return false;}
    const int canvas=b.canvas;
    if(tic!=b.drawnTic||!b.key) {
        static const byte *palette=nullptr,*colormap=nullptr;
        static byte reds[4];
        if(!palette) {
            palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_STATIC);colormap=(const byte*)W_CacheLumpName((char*)"COLORMAP",PU_STATIC);
            reds[0]=nearest(palette,30,4,4);reds[1]=nearest(palette,140,8,8);reds[2]=nearest(palette,196,56,48);reds[3]=nearest(palette,84,10,8);
        }
        std::vector<byte> pixels((size_t)canvas*canvas*2,0);
        const float half=canvas/2.0f,t=(float)tic;
        auto shade=[&](byte colour,float fade){return colormap[std::clamp((int)(fade*18),0,31)*256+colour];};
        // The cloud, behind the pieces: a texel shows once the growing reach passes its own lump of noise.
        if(b.bleeds) {
            float reach=b.cloudReach*(1-std::exp(-(t+1)/b.cloudTau)),fade=std::clamp((t-b.cloudFadeAt)/b.cloudFadeLen,0.0f,1.0f);
            for(int j=0;j<canvas;++j)for(int i=0;i<canvas;++i) {
                float px=i+0.5f-half-b.hitX,pz=half-(j+0.5f)-b.hitZ;
                float along=px*b.shotX+pz*b.shotZ,side=-px*b.shotZ+pz*b.shotX;
                // A mist, not a ball: lumpy at its edge and thinning out toward it, bright red inside.
                float d=std::hypot(along/b.cloudStretch,side),lump=valueNoise((i+0.5f)/4,(j+0.5f)/4,b.seed^0x68e31da4u);
                float inner=d/std::max(reach,1.0f);
                if(d>=reach*(0.6f+0.6f*lump)||hashUnit(i,j,b.seed^tic)>0.9f*(1.1f-inner)||bayer[j&3][i&3]<fade*16)continue;
                byte red=inner<0.5f?(lump<0.5f?reds[2]:reds[1]):(lump<0.6f?reds[1]:reds[3]);
                size_t p=(size_t)j*canvas+i;
                pixels[2*p]=shade(bloodRemap(red,b.tint),fade);pixels[2*p+1]=255;
            }
        }
        for(const BurstPiece &piece:b.pieces) {
            float out=1-std::exp(-t/b.tau);
            float ox=piece.reachX*out,oz=piece.reachZ*out-b.sag*t*t;
            float fade=std::clamp((t-piece.fadeAt)/piece.fadeLen,0.0f,1.0f);
            int turn=piece.turnAt>=0&&tic>=piece.turnAt?piece.turn:0;
            for(size_t k=0;k<piece.x.size();++k) {
                float lx=piece.x[k]+0.5f-piece.cx,lz=piece.z[k]+0.5f-piece.cz;
                for(int r=0;r<(turn&3);++r) {float w=lx;lx=-lz;lz=w;}
                if(turn==4)lx=-lx;
                int i=(int)std::floor(half+piece.cx+ox+lx),j=(int)std::floor(half-(piece.cz+oz+lz));
                if(i<0||j<0||i>=canvas||j>=canvas)continue;
                if(bayer[(j+piece.ditherY)&3][(i+piece.ditherX)&3]<fade*16)continue;
                byte colour=piece.colour[k];
                if(b.bleeds&&piece.torn[k]&&tic>=1&&hashUnit(piece.x[k],piece.z[k]+97,b.seed)<0.7f)   // torn: meat
                    colour=b.guts?gutsColour(*b.guts,piece.x[k],piece.z[k],b.seed):bloodRemap(reds[hashUnit(piece.x[k],piece.z[k],b.seed)<0.6f?1:3],b.tint);
                size_t p=(size_t)j*canvas+i;
                pixels[2*p]=shade(colour,fade);pixels[2*p+1]=255;
            }
        }
        std::vector<byte> normals=spriteNormals(canvas,canvas,pixels.data());
        if(!b.key)b.key=burstKeyBase+(++burstSerial&0x07ffffff);
        auto found=images.find(b.key);
        if(found==images.end())images.emplace(b.key,upload(canvas,canvas,pixels.data(),0,0,nullptr,normals.data()));
        else {
            std::vector<byte> packed((size_t)canvas*canvas*4);
            for(size_t i=0;i<(size_t)canvas*canvas;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,canvas,canvas}},packed.data(),canvas*4);
            found->second.pixels=std::move(pixels);
        }
        b.drawnTic=tic;
    }
    card.key=b.key;card.size=canvas;card.half=canvas/2.0f;card.x=x;card.y=y;card.top=z+canvas/2.0f;
    card.u0=0;card.v0=0;card.u1=canvas;card.v1=canvas;
    return true;
}
}
