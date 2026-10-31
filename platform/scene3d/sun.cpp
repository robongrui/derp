/* The sun: where it stands over the level and how it shines, placed once
   as the level starts (weather.cpp) and fixed all map long, since its
   light is baked (bake_sun.cpp). Read from the sky texture: its brightest
   columns face the sun (sky.frag maps column x to yaw -x*2pi/(4*width)) and
   also give its tint; the elevation is fixed at 40 degrees, or (Game_Sun
   fromSky) follows the height sky.frag shows their brightness at: painted
   on the horizon it lights from 35 degrees, painted 45 degrees up or more
   from 60. Lower suns left walled courtyards wholly in shade. A dark sky
   shines weaker, softer, in its own colour: moonlight. The episode's mood
   can hide it, and a map's DERPINFO can put it where the map wants it
   (placeSun): then the sky gives only its colour and strength. */
#include "internal.h"

namespace doom3d {
std::array<float,3> sunTint={1,1,1};
float sunAzimuth[2]={1,0}; // Horizontal direction toward the sun (sunElevation high).
float sunDirection[3]={0,0,1};
float sunYaw=0,sunGlow=0;
// The light's height above the horizon, and the drawn disc's (Game_Sun).
float sunElevation=40*doomPi/180,sunDiscElevation=40*doomPi/180,sunShade=0.3f,sunGain=0;

namespace {
// The sun as the sky texture paints it.
struct SkySun { float yaw,elevation,discElevation,glow,shade,gain; std::array<float,3> tint; };
SkySun readSky() {
    SkySun out{0,40*doomPi/180,40*doomPi/180,0,0,0,{1,1,1}};
    const Image &sky=wallImage(skytexture);
    const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
    // Fourth-power luminance favors highlights over large mid-tone areas.
    std::vector<double> columns(sky.width);
    auto weight=[&](int x,int y,const byte *&rgb) {
        size_t i=(size_t)y*sky.width+x;rgb=palette+sky.pixels[2*i]*3;
        if(!sky.pixels[2*i+1])return 0.0;
        double luma=(0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2])/255.0;
        return luma*luma*luma*luma;
    };
    for(int x=0;x<sky.width;++x)for(int y=0;y<sky.height;++y) {const byte *rgb;columns[x]+=weight(x,y,rgb);}
    int best=0;double bestScore=-1;
    for(int x=0;x<sky.width;++x) {
        double score=0;
        for(int d=-4;d<=4;++d)score+=columns[(x+d+sky.width)%sky.width];
        if(score>bestScore) {bestScore=score;best=x;}
    }
    double tint[3]={},total=0;
    for(int d=-4;d<=4;++d)for(int y=0;y<sky.height;++y) {
        const byte *rgb;double w=weight((best+d+sky.width)%sky.width,y,rgb);
        for(int c=0;c<3;++c)tint[c]+=rgb[c]/255.0*w;
        total+=w;
    }
    double peak=std::max({tint[0],tint[1],tint[2],1e-9});
    for(int c=0;c<3;++c)out.tint[c]=total>0?(float)(0.5+0.5*tint[c]/peak):1.0f;
    game_sun game;Game_Sun(&game);out.shade=game.shade;out.gain=game.gain;
    float daylight=1;
    if(game.fromSky) {
        // How bright the sky is on average: a grey day sky about 0.34, a
        // dim red one 0.14, night blue under 0.1. Dark skies shine weaker,
        // with softer shadows, in their own (paled) colour: moonlight.
        double sum=0,color[3]={};
        for(int y=0;y<sky.height;++y)for(int x=0;x<sky.width;++x) {
            size_t i=(size_t)y*sky.width+x;if(!sky.pixels[2*i+1])continue;
            const byte *rgb=palette+sky.pixels[2*i]*3;
            sum+=(0.299*rgb[0]+0.587*rgb[1]+0.114*rgb[2])/255.0;
            for(int c=0;c<3;++c)color[c]+=rgb[c];
        }
        daylight=std::clamp((float)(sum/std::max(1,sky.width*sky.height))-0.06f,0.0f,0.24f)/0.24f;
        out.shade*=0.6f+0.4f*daylight;out.gain*=0.35f+0.65f*daylight;
        double top=std::max({color[0],color[1],color[2],1e-9});
        for(int c=0;c<3;++c)out.tint[c]=out.tint[c]*daylight+(float)(0.6+0.4*color[c]/top)*(1-daylight);
    }
    if(game.fromSky&&total>0) {
        // sky.frag: texture row fraction v shows at rise (0.70-v)/0.68.
        double rows=0;
        for(int d=-4;d<=4;++d)for(int y=0;y<sky.height;++y) {const byte *rgb;rows+=weight((best+d+sky.width)%sky.width,y,rgb)*(y+0.5);}
        float painted=std::atan((0.70f-(float)(rows/total/sky.height))/0.68f);
        out.elevation=(35+25*std::clamp(painted/(45*doomPi/180),0.0f,1.0f))*doomPi/180;
        out.discElevation=std::max(painted,3*doomPi/180);
    }
    out.yaw=-(best+0.5f)*2*doomPi/(4.0f*sky.width);
    float bright=(float)std::pow(total/(9.0*sky.height),0.25),glow=std::clamp((bright-0.2f)/0.4f,0.0f,1.0f);
    out.glow=glow*glow*(3-2*glow)*daylight; // no sun in a night sky
    return out;
}
}

void placeSun() {
    SkySun sun=readSky();
    // The map's own sun (DERPINFO sun = angle, height): where it stands, in
    // map angles (0 east, 90 north), the disc drawn there too.
    if(DerpInfo_Map.sun==1) {
        sun.yaw=DerpInfo_Map.sunAngle*(float)doomPi/180;
        sun.elevation=sun.discElevation=DerpInfo_Map.sunHeight*(float)doomPi/180;
    }
    // The player's hour and heat (climate.cpp): how high it stands, how it shines.
    climateSun(sun.elevation,sun.discElevation,sun.tint,sun.shade,sun.gain,sun.glow);
    sunYaw=sun.yaw;sunElevation=sun.elevation;sunDiscElevation=sun.discElevation;
    sunShade=sun.shade;sunGain=sun.gain;sunTint=sun.tint;
    sunGlow=levelMood().sun?sun.glow:0; // An episode with the sun hidden (Game_Mood).
    sunDirection[0]=std::cos(sunYaw)*std::cos(sunElevation);
    sunDirection[1]=std::sin(sunYaw)*std::cos(sunElevation);
    sunDirection[2]=std::sin(sunElevation);
    float flat=std::hypot(sunDirection[0],sunDirection[1]);
    sunAzimuth[0]=sunDirection[0]/flat;sunAzimuth[1]=sunDirection[1]/flat;
}
} // namespace doom3d
