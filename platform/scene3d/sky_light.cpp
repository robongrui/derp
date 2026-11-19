/* The sky's towers lit per texel (settings.skyLayers, sky_layers.cpp). The
   sun stands still all level, so their light is baked whenever the towers
   or the sun change (a new sky, the time of day): the sun's light on each
   texel's own point and normal, shadowed by any tower between it and the
   sun; the sky's light by how much of the sky the texel sees past the
   towers round it; and the haze's scattered light, strongest on the side
   toward the sun, which keeps towers seen against the sun from going flat.
   The painting repeats four times round the horizon but the sun comes from
   one side, so the bake is four paintings wide.

   RGBA8, 4*width x 2*height: R the near towers' sun light (with the
   scattered light), G their sky light, B and A the far towers'; a byte to
   1.5. sky/depth.glsl mixes the two by how strong the sun is now. The lower
   half holds where snow can lie (R near, B far: how flat the ground faces
   up) and how high on the towers the texel stands (G, A: floor 0 to the
   ring's tallest top 255); the weather decides live how much lies. */
#include "sky_towers.h"
#include <thread>

namespace doom3d {
namespace {
GpuTextureRef lightTexture;
unsigned litSerial=~0u;
float litYaw=1e9f,litElevation=1e9f;

byte encode(float v) {return (byte)std::lround(std::clamp(v/1.5f,0.0f,1.0f)*255);}

// Rock that is lumpy, not ribbed: value noise over the four paintings'
// columns and rows (wrapping round), in three sizes, seeded per ring. Its
// slope at a texel tilts the baked normal, so the light breaks up into
// knobs and hollows instead of running in the columns' vertical bands.
float lattice(int x,int y,int period,uint32_t seed) {
    uint32_t h=(uint32_t)(((x%period)+period)%period)*0x8DA6B343u^(uint32_t)y*0xD8163841u^seed*0xCB1AB31Fu;
    h^=h>>13;h*=0x85EBCA6Bu;h^=h>>16;
    return (h&0xFFFF)/65535.0f;
}
float valueNoise(float x,float y,int period,uint32_t seed) {
    int i=(int)std::floor(x),j=(int)std::floor(y);
    float fx=x-i,fy=y-j;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    float a=lattice(i,j,period,seed),b=lattice(i+1,j,period,seed),c=lattice(i,j+1,period,seed),d=lattice(i+1,j+1,period,seed);
    return (a+(b-a)*fx)+((c+(d-c)*fx)-(a+(b-a)*fx))*fy;
}
float rock(float X,float y,int around,uint32_t seed) {
    float sum=0;
    const float sizes[3]={16,7,3},weights[3]={0.5f,0.32f,0.18f};
    for(int k=0;k<3;++k)sum+=weights[k]*valueNoise(X/sizes[k],y/sizes[k]*1.3f,std::max(1,(int)(around/sizes[k])),seed+k*101);
    return sum;
}

// One texel's light (sun with scatter, sky), at column X of the four
// paintings round and row y, on ring's towers; false where no tower stands.
bool texelLight(const Towers &T,int X,int y,int ring,const float sun[3],float &direct,float &sky,float &flatness,float &high) {
    const SkyPaint &p=T.p;
    int x=X%p.w;
    const byte *t=T.texel(p.h+2+ring,x);
    if(!t[3])return false;
    float r=ring?skyFarRing:1,floor=-skyFloorDrop,cov=T.texel(p.h,x)[ring];
    float depth=(0.5f+t[3]/255.0f*1.5f)*r,along=(t[0]-128)/127.0f,top=T.rise((int)cov)*depth;
    float shape=T.rise(T.texel(p.h,x)[ring+2])*depth;
    if(top<=floor)return false;
    float reach=skyFootReach*(shape-floor)*(0.55f+0.45f*std::sqrt(std::max(1-along*along,0.0f)));
    // Where on the tower's front this texel lies: sky/depth.glsl's
    // towerColor counts rows by how far under the painted top, or out from
    // it on flat ground; the same walked back from the row, on the face
    // shaped from the smoothed crest and cut off at the painted one.
    auto heightAt=[&](float u){return std::min(floor+(shape-floor)*Towers::profile(u),top);};
    float rowHeight=depth*skyShrink/(skyRowScale*p.h),fromTop=std::max(0.0f,y+0.5f-cov)*rowHeight;
    auto counted=[&](float u){return std::max(top-heightAt(u),u*reach);};
    float lo=0,hi=1;
    if(counted(1)<=fromTop)lo=hi;
    else for(int k=0;k<16;++k){float m=(lo+hi)/2;if(counted(m)<fromTop)lo=m;else hi=m;}
    float u=(lo+hi)/2,theta=-(X+0.5f)*2*doomPi/(4.0f*p.w),rho=depth-u*reach;
    float out2[2]={std::cos(theta),std::sin(theta)},way[3]={-out2[1],out2[0],0};
    float point[3]={out2[0]*rho,out2[1]*rho,heightAt(u)};
    // Its normal: flat where the painted crest cuts the face off, else the
    // smooth face toward the eye and along the crest (the smoothed crest's
    // slope over seven columns), tilted by the normals read from the
    // painted shading.
    bool cut=floor+(shape-floor)*Towers::profile(u)>top;
    float fallBy=u>0&&!cut?-(shape-floor)*std::min(Towers::fall(u),8.0f)/reach:0;
    float crestSlope=cut?0:(T.rise(T.texel(p.h,x-3)[ring+2])-T.rise(T.texel(p.h,x+3)[ring+2]))*depth/(6*2*doomPi/(4.0f*p.w)*std::max(rho,1e-4f));
    float n[3]={out2[0]*fallBy-way[0]*crestSlope*Towers::profile(u),out2[1]*fallBy-way[1]*crestSlope*Towers::profile(u),1};
    auto normalize=[](float v[3]){float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(l>0)for(int c=0;c<3;++c)v[c]/=l;};
    normalize(n);
    const byte *pn=&p.normals[4*(y*p.w+x)+(ring?2:0)];
    float tilt[2]={pn[0]/255.0f*2-1,pn[1]/255.0f*2-1};
    float up[3]={-n[0]*n[2],-n[1]*n[2],1-n[2]*n[2]};
    normalize(up);
    float keep=std::sqrt(std::max(1-tilt[0]*tilt[0]-tilt[1]*tilt[1],0.0f));
    for(int c=0;c<3;++c)n[c]=n[c]*keep-tilt[0]*way[c]-tilt[1]*up[c];
    normalize(n);
    // The rock's lumps (rock), raised where the noise is high: its slope
    // across the columns (which run the other way round) and down the rows.
    uint32_t seed=0x5EEDu+ring*7919u;
    float gx=(rock(X+1.0f,(float)y,4*p.w,seed)-rock(X-1.0f,(float)y,4*p.w,seed))*0.5f;
    float gy=(rock((float)X,y+1.0f,4*p.w,seed)-rock((float)X,y-1.0f,4*p.w,seed))*0.5f;
    const float lumps=12.0f;
    for(int c=0;c<3;++c)n[c]+=lumps*(gx*way[c]+gy*up[c]);
    normalize(n);
    for(int c=0;c<3;++c)point[c]+=n[c]*0.003f;
    float facing=n[0]*sun[0]+n[1]*sun[1]+n[2]*sun[2];
    direct=facing>0&&T.clear(point,sun,4.5f,48)?facing:0;
    // The sky it sees: eight ways round at 35 degrees up and straight up,
    // each as much as the texel faces it.
    float seen=0,weight=0;
    for(int k=0;k<9;++k) {
        float a=k<8?(k+0.5f)*doomPi/4:0,e=k<8?35*doomPi/180:doomPi/2;
        float d[3]={std::cos(a)*std::cos(e),std::sin(a)*std::cos(e),std::sin(e)};
        float w=n[0]*d[0]+n[1]*d[1]+n[2]*d[2];
        if(w<=0)continue;
        weight+=w;
        if(T.clear(point,d,2.0f,24))seen+=w;
    }
    float open=weight>0?seen/weight:1;
    sky=open*(0.7f+0.3f*n[2]);
    // The haze's scattered light, from the bright side of the sky.
    float flat=std::hypot(sun[0],sun[1]),toward=flat>0?(n[0]*sun[0]+n[1]*sun[1])/flat:0;
    direct+=0.3f*open*(0.5f+0.5f*std::max(toward,0.0f));
    // Snow lies on ground that faces up enough, the more the flatter.
    flatness=std::clamp((n[2]-0.15f)/0.5f,0.0f,1.0f);   // karst is steep: ledges and slopes hold some too
    high=std::clamp((point[2]-floor)/std::max(p.maxTop[ring]*r-floor,1e-4f),0.0f,1.0f);
    return true;
}

void bakeLight() {
    const SkyPaint &p=skyPaint();
    litSerial=p.serial;litYaw=sunYaw;litElevation=sunDiscElevation;
    const byte none[4]={encode(0.5f),encode(0.9f),encode(0.5f),encode(0.9f)};
    lightTexture=gpuCreateTexture(GpuFormat::RGBA8,1,1,none,4);
    if(p.w<=0||p.texels.empty())return;
    int W=4*p.w;
    std::vector<byte> out(4*(size_t)W*2*p.h,0);
    for(size_t i=0;i<4*(size_t)W*p.h;i+=4)std::copy(none,none+4,&out[i]);
    Towers T{p};
    float sun[3]={std::cos(sunYaw)*std::cos(sunDiscElevation),std::sin(sunYaw)*std::cos(sunDiscElevation),std::sin(sunDiscElevation)};
    // Rows split over the CPU's threads; load time is free, but a time of
    // day picked in the menu shouldn't stall long.
    int threads=std::clamp((int)std::thread::hardware_concurrency(),1,16);
    std::vector<std::thread> pool;
    for(int k=0;k<threads;++k)pool.emplace_back([&,k] {
        for(int y=k;y<p.h;y+=threads)for(int X=0;X<W;++X)for(int ring=0;ring<(p.far?2:1);++ring) {
            int x=X%p.w;
            if(!(p.texels[4*(y*p.w+x)+3]&(ring?1:2)))continue;
            float direct,sky,flat,high;
            if(!texelLight(T,X,y,ring,sun,direct,sky,flat,high))continue;
            byte *o=&out[4*((size_t)y*W+X)+ring*2],*snow=&out[4*((size_t)(y+p.h)*W+X)+ring*2];
            o[0]=encode(direct);o[1]=encode(sky);
            snow[0]=(byte)std::lround(flat*255);snow[1]=(byte)std::lround(high*255);
        }
    });
    for(std::thread &t:pool)t.join();
    // The far towers' light softened, five columns across and three rows
    // down: kinks in their crest ran down the faces as dark lines, one
    // column wide, that read as a broken texture through the haze. Their
    // texels stay sharp; only the light is soft, as from that far it is.
    if(p.far) {
        std::vector<byte> soft(out);
        static const int across[5]={1,2,3,2,1},down[3]={1,2,1};
        for(int y=0;y<p.h;++y)for(int X=0;X<W;++X)for(int ch=2;ch<4;++ch) {
            int sum=0,weight=0;
            for(int j=-1;j<=1;++j) {
                int yy=y+j;
                if(yy<0||yy>=p.h)continue;
                for(int i=-2;i<=2;++i) {
                    int k=across[i+2]*down[j+1];
                    sum+=k*out[4*((size_t)yy*W+(X+i+W)%W)+ch];weight+=k;
                }
            }
            soft[4*((size_t)y*W+X)+ch]=(byte)((sum+weight/2)/weight);
        }
        out.swap(soft);
    }
    lightTexture=gpuCreateTexture(GpuFormat::RGBA8,W,2*p.h,out.data(),4*W);
}
} // namespace

void skyLightUpdate() {
    const SkyPaint &p=skyPaint();
    if(!lightTexture||litSerial!=p.serial||std::abs(litYaw-sunYaw)>1e-4f||std::abs(litElevation-sunDiscElevation)>1e-4f)bakeLight();
}
const GpuTextureRef &skyLayerLight() {
    if(!lightTexture)skyLightUpdate();
    return lightTexture;
}
void skyLightShutdown() {lightTexture=nullptr;litSerial=~0u;litYaw=litElevation=1e9f;}
} // namespace doom3d
