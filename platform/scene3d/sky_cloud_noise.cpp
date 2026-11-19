/* The noise real clouds are made of (sky/cloud_volume.glsl): a small tiling
   volume, R Perlin-Worley for the billowing body, G Worley at finer sizes to eat
   its edges. The recipe is Guerrilla's for Horizon Zero Dawn (Schneider and
   Vos, SIGGRAPH 2015): Perlin keeps the shapes joined up, Worley dilates them
   into round billows. A painting says where cloud goes; this says what cloud
   looks like, which a smooth map never can. Made once, when a sky first asks. */
#include "sky_paint.h"

namespace doom3d {
namespace {
constexpr int size=64;
GpuTextureRef texture;
bool real=false;   // not the empty stand-in
std::vector<byte> voxels;

float fade(float t) {return t*t*t*(t*(t*6-15)+10);}
uint32_t cellHash(int x,int y,int z,int period,uint32_t seed) {
    x=(x%period+period)%period;y=(y%period+period)%period;z=(z%period+period)%period;
    return skyHash(seed,x+y*521+z*271829);
}
// Gradient noise tiling every period cells, about -1 to 1.
float perlin(float x,float y,float z,int period,uint32_t seed) {
    int i=(int)std::floor(x),j=(int)std::floor(y),k=(int)std::floor(z);
    float fx=x-i,fy=y-j,fz=z-k,u=fade(fx),v=fade(fy),w=fade(fz);
    auto corner=[&](int a,int b,int c) {
        uint32_t h=cellHash(i+a,j+b,k+c,period,seed)&15;
        float dx=fx-a,dy=fy-b,dz=fz-c;
        float p=h<8?dx:dy,q=h<4?dy:h==12||h==14?dx:dz;
        return ((h&1)?-p:p)+((h&2)?-q:q);
    };
    auto mix=[](float a,float b,float t){return a+(b-a)*t;};
    return mix(mix(mix(corner(0,0,0),corner(1,0,0),u),mix(corner(0,1,0),corner(1,1,0),u),v),
               mix(mix(corner(0,0,1),corner(1,0,1),u),mix(corner(0,1,1),corner(1,1,1),u),v),w);
}
// Cellular noise tiling every period cells: 1 on a cell's point, falling away.
float worley(float x,float y,float z,int period,uint32_t seed) {
    int i=(int)std::floor(x),j=(int)std::floor(y),k=(int)std::floor(z);
    float best=9;
    for(int c=-1;c<=1;++c)for(int b=-1;b<=1;++b)for(int a=-1;a<=1;++a) {
        uint32_t h=cellHash(i+a,j+b,k+c,period,seed);
        float px=i+a+(h&1023)/1023.0f,py=j+b+((h>>10)&1023)/1023.0f,pz=k+c+((h>>20)&1023)/1023.0f;
        float d=(px-x)*(px-x)+(py-y)*(py-y)+(pz-z)*(pz-z);
        best=std::min(best,d);
    }
    return 1-std::min(1.0f,std::sqrt(best));
}
float worleyOctaves(float u,float v,float w,int period,uint32_t seed) {
    return 0.625f*worley(u*period,v*period,w*period,period,seed)
          +0.25f*worley(u*period*2,v*period*2,w*period*2,period*2,seed+1)
          +0.125f*worley(u*period*4,v*period*4,w*period*4,period*4,seed+2);
}
// Stretched to fill 0-1, so a density threshold means the same on any noise.
void stretch(std::vector<float> &values) {
    std::vector<float> sorted(values);
    std::sort(sorted.begin(),sorted.end());
    float low=sorted[sorted.size()/50],high=sorted[sorted.size()*49/50];
    for(float &v:values)v=std::clamp((v-low)/std::max(high-low,1e-4f),0.0f,1.0f);
}
void make() {
    size_t n=(size_t)size*size*size;
    std::vector<float> body(n),edge(n);
    // A slice a job, on all cores: on one this was two thirds of a second in the first level's load.
    parallelFor(size,[&](int z) {for(int y=0;y<size;++y)for(int x=0;x<size;++x) {
        float u=(x+0.5f)/size,v=(y+0.5f)/size,w=(z+0.5f)/size;
        float p=0,part=0.5f;
        for(int o=0,f=4;o<4;++o,f*=2,part*=0.5f)p+=part*perlin(u*f,v*f,w*f,f,0x51C0u+o);
        p=std::clamp(p*0.9f+0.5f,0.0f,1.0f);
        float shape=worleyOctaves(u,v,w,4,0xC10Du);
        size_t i=((size_t)z*size+y)*size+x;
        body[i]=shape+p*(1-shape);   // Perlin dilated by Worley
        edge[i]=worleyOctaves(u,v,w,8,0xED6Eu);
    }});
    stretch(body);stretch(edge);
    voxels.resize(2*n);
    for(size_t i=0;i<n;++i){voxels[2*i]=(byte)std::lround(255*body[i]);voxels[2*i+1]=(byte)std::lround(255*edge[i]);}
}
} // namespace

void skyCloudNoiseMake() {
    if(voxels.empty())make();
    if(!real)texture=gpuCreateVolume(GpuFormat::RG8,size,size,size,voxels.data());
    real=true;
}
const GpuTextureRef &skyCloudNoise() {
    if(!texture) {
        const byte empty[2]={0,0};
        texture=gpuCreateVolume(GpuFormat::RG8,1,1,1,empty);
    }
    return texture;
}
void skyCloudNoiseShutdown() {texture=nullptr;real=false;}
} // namespace doom3d
