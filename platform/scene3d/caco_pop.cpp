/* The balloon (gore): a rocket straight into a Cacodemon's open mouth, and it kills him.
   It goes off inside. He stops where he is, mouth open, and swells, the belly first,
   drawn afresh each tic in square texels (nearest, never a smeared sprite), shaking at
   the end; then he pops from the inside (body_burst.cpp) and it rains. Drawing only:
   the game's corpse falls as ever behind the picture. */
#include "internal.h"
extern "C" {
#include "../audio/impact_synth.h"
#include "../audio/sound_options.h"
}

namespace doom3d {
constexpr int balloonKeyBase=-0x73000000;
constexpr int balloonTics=75;              // how long he swells: two seconds and more, the agony of it
constexpr float balloonMost=0.8f;          // how far: his middle this share past where it was
constexpr int balloonShakeFrom=52;         // from here he shakes, a texel either way
static int balloonSerial=0;

struct Balloon {
    int width=0,height=0,left=0,bottom=0;std::vector<byte> colour,solid;   // his drawing on a grid, its middle at (cx,cz)
    float cx=0,cz=0,reach=1,floor=-1e9f;   // and the floor under him, which he swells up off
    int canvas=64,key=0,drawn=-1;
    uint32_t seed=0;
};

std::shared_ptr<Balloon> startBalloon(const std::vector<HeadTexel> &texels,float x,float y,float z,uint32_t seed) {
    if(texels.size()<16)return nullptr;
    auto balloon=std::make_shared<Balloon>();Balloon &b=*balloon;
    int x0=INT_MAX,x1=INT_MIN,z0=INT_MAX,z1=INT_MIN;double mx=0,mz=0;
    for(const HeadTexel &t:texels) {x0=std::min(x0,t.x);x1=std::max(x1,t.x);z0=std::min(z0,t.z);z1=std::max(z1,t.z);mx+=t.x+0.5;mz+=t.z+0.5;}
    b.left=x0;b.bottom=z0;b.width=x1-x0+1;b.height=z1-z0+1;
    b.cx=(float)(mx/texels.size());b.cz=(float)(mz/texels.size());
    b.colour.assign((size_t)b.width*b.height,0);b.solid.assign((size_t)b.width*b.height,0);
    for(const HeadTexel &t:texels) {
        size_t i=(size_t)(t.z-z0)*b.width+(t.x-x0);
        b.colour[i]=t.colour;b.solid[i]=1;
        b.reach=std::max(b.reach,std::hypot(t.x+0.5f-b.cx,t.z+0.5f-b.cz));
    }
    b.canvas=std::clamp(((int)std::ceil(2*b.reach*(1+balloonMost))+11)/4*4,64,320);
    b.seed=seed;
    b.floor=floorZ(R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector);
    I_MadeSound(IMPACT_BALLOON,x,y,z);
    return balloon;
}
int balloonLasts() {return balloonTics;}
// How far he is at the tic: slow at first, faster as he goes.
static float swell(int tic) {
    float t=std::clamp(tic/(float)balloonTics,0.0f,1.0f);
    return balloonMost*t*t*(1+0.05f*std::sin(tic*0.55f));   // in heaves, not one smooth pull
}

// His picture at the tic, its middle at (x,y,z) less his own middle's height: each pixel looked up back through the
// bulge (more in his middle than at his rim), nearest texel.
bool balloonCard(Balloon &b,int tic,float x,float y,float z,RagdollCard &card) {
    const int canvas=b.canvas;
    if(tic!=b.drawn||!b.key) {
        std::vector<byte> pixels((size_t)canvas*canvas*2,0);
        float g=swell(tic),half=canvas/2.0f;
        int shakeX=0,shakeZ=0;
        if(tic>=balloonShakeFrom) {shakeX=(int)std::lround(hashUnit(tic,1,b.seed)*2-1);shakeZ=(int)std::lround(hashUnit(tic,2,b.seed)*2-1);}
        for(int j=0;j<canvas;++j)for(int i=0;i<canvas;++i) {
            float px=i+0.5f-half-shakeX,pz=half-(j+0.5f)-shakeZ,d=std::hypot(px,pz);
            // (Past his rim the bulge turns back on itself: without this the canvas's corners showed bits of him.)
            if(d>b.reach*(1+0.55f*g)+1)continue;
            // Back through r' = r (1 + g (1 - 0.45 (r/R)^2)): a few steps from the plain scale.
            float r=d/(1+g);
            for(int k=0;k<3;++k) {float q=r/b.reach;r=d/(1+g*(1-0.45f*q*q));}
            float scale=d>1e-3f?r/d:0;
            int tx=(int)std::floor(b.cx+px*scale)-b.left,tz=(int)std::floor(b.cz+pz*scale)-b.bottom;
            if(tx<0||tz<0||tx>=b.width||tz>=b.height)continue;
            size_t t=(size_t)tz*b.width+tx;
            if(!b.solid[t])continue;
            size_t p=(size_t)j*canvas+i;
            pixels[2*p]=b.colour[t];pixels[2*p+1]=255;
        }
        std::vector<byte> normals=spriteNormals(canvas,canvas,pixels.data());
        if(!b.key)b.key=balloonKeyBase-(++balloonSerial&0x00ffffff);
        auto found=images.find(b.key);
        if(found==images.end())images.emplace(b.key,upload(canvas,canvas,pixels.data(),0,0,nullptr,normals.data()));
        else {
            std::vector<byte> packed((size_t)canvas*canvas*4);
            for(size_t i=0;i<(size_t)canvas*canvas;++i) {packed[4*i]=pixels[2*i];packed[4*i+1]=pixels[2*i+1];packed[4*i+2]=normals[2*i];packed[4*i+3]=normals[2*i+1];}
            gpuUpdateTexture(found->second.texture,GpuFormat::RGBA8,{{0,0,canvas,canvas}},packed.data(),canvas*4);
            found->second.pixels=std::move(pixels);
        }
        b.drawn=tic;
    }
    // Low down he swells upward: the floor flattened his underside.
    z=std::max(z,b.floor+b.reach*(1+0.55f*swell(tic)));
    card.key=b.key;card.size=canvas;card.half=canvas/2.0f;card.x=x;card.y=y;card.top=z+canvas/2.0f;
    card.u0=0;card.v0=0;card.u1=canvas;card.v1=canvas;card.shadow=false;   // (a full canvas as a shadow threw long wedges in the blast's light)
    return true;
}
void dropBalloon(Balloon &b) {
    if(!b.key)return;
    images.erase(b.key);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches})group->erase(b.key);
    shadowBatches.erase(-1-b.key);decalBatches.erase(-1-b.key);
    b.key=0;
}
}
