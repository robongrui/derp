/* The painted cloud field: a stock sky's own clouds as columns in a shell
   (sky/cloud_field.glsl marches them). From the painting, as the level loads:
   how much each texel stands clear of its row's clear sky is how much cloud
   lies there, its place among the painting's colours is its tone, and seeded
   billows give the columns a height the painting can't. The rows are blended
   into a seamless tile, twice the painting's size. Sun and self-shadow go into
   the tone here, once, and again only when the sun or the wind turn: the shader
   marches with no light of its own, and the shadows ride along with the cloud.

   Laid into sky_layers.cpp's first settings row: texel 12 on, thickness, fade
   from and to, 13 texel width, stretch, opacity, flags, 14 how much of the
   painting shows behind, the mountains' shrink, air and hard rock, 15 the low
   banks, where real sky ends, the scud, 16-31 the sixteen ramp colours. */
#include "sky_profiles.h"

namespace doom3d {
namespace {
struct Field {
    int w=0,h=0;
    SkyCloudProfile profile{};
    std::vector<float> amount,top,base,tone;
} field;
GpuTextureRef texture;
float litYaw=1e9f,litElevation=1e9f,litWind[2]={1e9f,1e9f};

float luma(const SkyRgb &c) {return (0.299f*c.r+0.587f*c.g+0.114f*c.b)/255.0f;}
float smooth(float a,float b,float x) {x=std::clamp((x-a)/(b-a),0.0f,1.0f);return x*x*(3-2*x);}
// Value noise periodic over px by py cells, u and v in turns of that.
float cellNoise(float u,float v,int px,int py,uint32_t seed) {
    float x=u*px,y=v*py;
    int i=(int)std::floor(x),j=(int)std::floor(y);
    float fx=x-i,fy=y-j;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    auto at=[&](int a,int b) {
        a=((a%px)+px)%px;b=((b%py)+py)%py;
        return (float)(skyHash(seed,a+b*4099)&0xFFFF)/65535.0f;
    };
    return (at(i,j)*(1-fx)+at(i+1,j)*fx)*(1-fy)+(at(i,j+1)*(1-fx)+at(i+1,j+1)*fx)*fy;
}
float billows(float u,float v,uint32_t seed) {
    float sum=0,weight=0,part=0.5f;
    for(int o=0,px=12,py=12;o<4;++o,px*=2,py*=2,part*=0.55f) {sum+=part*cellNoise(u,v,px,py,seed+o);weight+=part;}
    return sum/weight;
}
// Rows blended into a period p of them: the last h-p run into the first.
std::vector<float> seamless(const std::vector<float> &from,int w,int h,int p) {
    std::vector<float> out((size_t)w*p);
    int k=h-p;
    for(int y=0;y<p;++y)for(int x=0;x<w;++x) {
        float v=from[(size_t)y*w+x];
        if(y<k)v=from[(size_t)(y+p)*w+x]*(1-smooth(0,1,(float)y/k))+v*smooth(0,1,(float)y/k);
        out[(size_t)y*w+x]=v;
    }
    return out;
}
std::vector<float> blurred(const std::vector<float> &from,int w,int h) {
    static const int k[3]={1,2,1};
    std::vector<float> out(from.size());
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        float sum=0;
        for(int j=-1;j<=1;++j)for(int i=-1;i<=1;++i)sum+=k[i+1]*k[j+1]*from[(size_t)((y+j+h)%h)*w+(x+i+w)%w];
        out[(size_t)y*w+x]=sum/16;
    }
    return out;
}
// Twice the size, between the texels.
float between(const std::vector<float> &from,int w,int h,float x,float y) {
    int i=(int)std::floor(x),j=(int)std::floor(y);
    float fx=x-i,fy=y-j;
    auto at=[&](int a,int b){return from[(size_t)((b+h)%h)*w+(a+w)%w];};
    return (at(i,j)*(1-fx)+at(i+1,j)*fx)*(1-fy)+(at(i,j+1)*(1-fx)+at(i+1,j+1)*fx)*fy;
}

// Sun and self-shadow into the tone, and the field to the GPU.
void relight() {
    float wx,wy;windHeading(wx,wy);
    float length=std::hypot(wx,wy);
    if(length>1e-3f){wx/=length;wy/=length;}else{wx=0.94f;wy=0.34f;}   // sky/clouds.glsl's windWay
    litYaw=sunYaw;litElevation=sunDiscElevation;litWind[0]=wx;litWind[1]=wy;
    const int w=field.w,h=field.h;
    const SkyCloudProfile &p=field.profile;
    float sx=std::cos(sunYaw)*std::cos(sunDiscElevation),sy=std::sin(sunYaw)*std::cos(sunDiscElevation),sz=std::sin(sunDiscElevation);
    float along=sx*wx+sy*wy,across=-sx*wy+sy*wx;
    float tx=p.texel*p.stretch,ty=p.texel,up=p.thickness;   // all in near-ring radii
    float reach=2.0f*p.texel;
    auto index=[&](int x,int y){return (size_t)((y%h+h)%h)*w+(x%w+w)%w;};
    std::vector<byte> out(4*(size_t)w*h);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        size_t i=index(x,y);
        float shade=1,facing=0.5f;
        if(sz>0.02f&&field.amount[i]>0) {
            float blocked=0;
            for(int k=1;k<=10;++k) {
                float d=reach*k,z=field.top[i]+sz*d/up;
                if(z>=1)break;
                size_t n=index(x+(int)std::lround(along*d/tx),y+(int)std::lround(across*d/ty));
                if(z>field.base[n]&&z<field.top[n])blocked+=field.amount[n];
            }
            shade=std::exp(-0.45f*blocked);
            float gx=(field.top[index(x+1,y)]-field.top[index(x-1,y)])*up/(2*tx),gy=(field.top[index(x,y+1)]-field.top[index(x,y-1)])*up/(2*ty);
            float n=1/std::sqrt(gx*gx+gy*gy+1);
            facing=std::clamp((-gx*along-gy*across+sz)*n,0.0f,1.0f);
        }
        float tone=field.tone[i]*(0.6f+0.4f*shade)+0.25f*(facing-0.5f);
        byte *t=&out[4*i];
        t[0]=(byte)std::lround(255*field.amount[i]);t[1]=(byte)std::lround(255*field.top[i]);
        t[2]=(byte)std::lround(255*field.base[i]);t[3]=(byte)std::lround(255*std::clamp(tone,0.0f,1.0f));
    }
    texture=gpuCreateTexture(GpuFormat::RGBA8,w,h,out.data(),4*w);
}
} // namespace

void skyCloudFieldBake(const SkyImage &s,const SkyCloudProfile &p,std::vector<byte> &texels) {
    const int w=s.w,h=p.skyRows?std::min(p.skyRows,s.h):s.h,n=w*h;
    // Each texel's place among the painting's colours, and the ramp: the
    // sixteen sixteenths of them, darkest to brightest.
    std::vector<float> light(n),sorted;
    for(int i=0;i<n;++i)light[i]=luma(s.rgb[i]);
    sorted=light;std::sort(sorted.begin(),sorted.end());
    std::vector<int> order(n);
    for(int i=0;i<n;++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](int a,int b){return light[a]<light[b];});
    // In full colour, softened from one to the next: as palette indices its
    // steps showed in the cloud as contour lines.
    SkyRgb ramp[16];
    for(int k=0;k<16;++k) {
        SkyRgb m{0,0,0};
        int top=p.rampTop>0?(int)(n*p.rampTop):n,from=top*k/16,to=top*(k+1)/16;
        for(int i=from;i<to;++i){const SkyRgb &c=s.rgb[order[i]];m.r+=c.r/(to-from);m.g+=c.g/(to-from);m.b+=c.b/(to-from);}
        float shade=p.rampLow>0?p.rampLow+(1-p.rampLow)*std::min(k/9.0f,1.0f):1;
        ramp[k]={m.r*shade,m.g*shade,m.b*shade};
    }
    byte *colours=&texels[4*(w*(s.h+1)+16)];
    for(int k=0;k<16;++k) {
        const SkyRgb &a=ramp[std::max(k-1,0)],&b=ramp[k],&c=ramp[std::min(k+1,15)];
        colours[4*k]=(byte)std::lround((a.r+2*b.r+c.r)/4);colours[4*k+1]=(byte)std::lround((a.g+2*b.g+c.g)/4);
        colours[4*k+2]=(byte)std::lround((a.b+2*b.b+c.b)/4);colours[4*k+3]=255;
    }
    byte *settings=&texels[4*(w*(s.h+1)+8)];
    settings[16]=1;settings[17]=(byte)std::lround(p.thickness*256);
    settings[18]=(byte)std::lround(p.fadeFrom*128);settings[19]=(byte)std::lround(p.fadeTo*128);
    settings[20]=(byte)std::lround(p.texel*4096);settings[21]=(byte)std::lround(p.stretch*16);
    settings[22]=(byte)std::lround(p.opacity*8);settings[23]=p.cirrus?1:0;
    settings[24]=(byte)std::lround(255*(p.backdrop>0?p.backdrop:1.0f));
    settings[25]=(byte)std::lround(255*p.shrink);settings[26]=(byte)std::lround(255*p.distance);settings[27]=p.paintedRock?255:0;
    settings[28]=(byte)std::lround(255*p.bank);settings[30]=(byte)std::lround(255*p.scud);settings[31]=(byte)std::lround(255*p.cover);
    skyCloudNoiseMake();
    // The clear sky of each row, softened down the painting.
    std::vector<float> clear(h),soft(h);
    for(int y=0;y<h;++y) {
        std::vector<float> row(light.begin()+y*w,light.begin()+(y+1)*w);
        std::sort(row.begin(),row.end());
        clear[y]=row[(int)(p.clear*(w-1))];
    }
    for(int y=0;y<h;++y) {
        float sum=0;int count=0;
        for(int d=-6;d<=6;++d)if(y+d>=0&&y+d<h){sum+=clear[y+d];++count;}
        soft[y]=sum/count;
    }
    std::vector<float> cloud(n),tone(n);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int i=y*w+x;
        float d=light[i]-soft[y];
        cloud[i]=d<0?-d*p.darkCloud:d*p.brightCloud;
        tone[i]=(float)(std::lower_bound(sorted.begin(),sorted.end(),light[i])-sorted.begin())/n;
    }
    int period=h-h/5;
    cloud=blurred(seamless(cloud,w,h,period),w,period);
    tone=blurred(seamless(tone,w,h,period),w,period);
    field.w=2*w;field.h=2*period;field.profile=p;
    size_t size=(size_t)field.w*field.h;
    field.amount.assign(size,0);field.top.assign(size,0);field.base.assign(size,0);field.tone.assign(size,0);
    uint32_t seed=0xC10D5u;
    for(int y=0;y<field.h;++y)for(int x=0;x<field.w;++x) {
        float u=(float)x/field.w,v=(float)y/field.h,fold=billows(u,v,seed);
        float amount=smooth(0.1f,1.0f,between(cloud,w,period,x*0.5f,y*0.5f)+(fold-0.5f)*0.5f);
        float thick=std::min(1.0f,std::pow(amount,0.6f)*(0.45f+1.1f*fold));
        size_t i=(size_t)y*field.w+x;
        field.amount[i]=amount;field.top[i]=0.5f+0.5f*thick;field.base[i]=0.5f-0.2f*thick;
        field.tone[i]=std::clamp(between(tone,w,period,x*0.5f,y*0.5f)+(fold-0.5f)*0.7f*amount,0.0f,1.0f);
    }
    relight();
}

void skyCloudFieldUpdate() {
    if(!field.w)return;
    float wx,wy;windHeading(wx,wy);
    float length=std::hypot(wx,wy);
    if(length>1e-3f){wx/=length;wy/=length;}else{wx=0.94f;wy=0.34f;}
    if(std::abs(litYaw-sunYaw)>1e-4f||std::abs(litElevation-sunDiscElevation)>1e-4f||std::abs(litWind[0]-wx)>1e-3f||std::abs(litWind[1]-wy)>1e-3f)relight();
}
const GpuTextureRef &skyCloudField() {
    if(!texture) {
        const byte empty[4]={0,0,0,0};
        texture=gpuCreateTexture(GpuFormat::RGBA8,1,1,empty,4);
    }
    return texture;
}
void skyCloudFieldClear() {field=Field{};texture=nullptr;litYaw=litElevation=1e9f;}
} // namespace doom3d
