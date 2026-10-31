/* The flashlight's beam in the weather (lights.cpp's first light while the
   flashlight is on, a cone from the shoulder):
   - what flies through it lights up: weatherColor asks beamAt for every
     flake, drop and grain, by the beam's cone and falloff as the lamp lights
     surfaces; a few tumbling snowflakes and grains glint white for three
     tics (beamGlint).
   - the air it crosses glows, as thick as what hangs in it out in the open
     (beamUniforms into c.beam, c.beamAim, c.beamLight): a blizzard's snow,
     the blowing sand, the ash; rain hardly scatters a beam, only its drops
     light. The shaders gather it along the view (world.frag's walk to the
     point, sky.frag's to the sky), and the fog bank's fog and dry ice light
     up where it passes through them (world/fog_march.glsl), stage fog.
   - lying snow and sand glitter in it (world.frag, beam_air.glsl).
   No wall test is needed for any of it: the lamp rides at the eye, and what
   the eye sees the beam reaches. Drawing only. */
#include "internal.h"

namespace doom3d {
namespace {
// The beam while the flashlight is on, or none.
const Flash *beam() {
    if(!flashlightOn||flashes.count==0)return nullptr;
    const Flash &f=flashes.lights[0];
    return f.direction[3]>0?&f:nullptr;
}
}

float beamAt(float x,float y,float z,std::array<float,3> *color) {
    const Flash *f=beam();
    if(!f)return 0;
    float r=doom_flash_reach(x-f->position[0],y-f->position[1],z-f->position[2],f);
    if(color)for(int c=0;c<3;++c)(*color)[c]=f->color[c];
    return f->strength*r*r*(3-2*r);
}

void beamGlint(float x,float y,float z,uint32_t seed,float rate,std::array<float,3> &color) {
    std::array<float,3> light;
    float b=beamAt(x,y,z,&light);
    // Tumbling, it turns a facet to the lamp now and then: held three tics.
    if(b<0.15f||hashUnit(leveltime/3,7,seed)>=rate*std::min(1.0f,b))return;
    for(int c=0;c<3;++c)color[c]=std::max(color[c],std::min(1.0f,0.75f+0.25f*light[c]));
}

void beamUniforms(Uniforms &camera) {
    Weather kind=settings.weather?falling():WEATHER_NONE;
    // How much of the open air is round the eye: the whiteout's (whiteout.glsl)
    // and the beam's share of the way indoors (weather.cpp).
    camera.climate[3]=kind!=WEATHER_NONE?openAirRound(camera):0.0f;
    for(int c=0;c<4;++c)camera.beam[c]=camera.beamAim[c]=camera.beamLight[c]=0;
    const Flash *f=beam();
    if(!f||kind==WEATHER_NONE||camera.effects[2]!=0)return;
    for(int c=0;c<3;++c) {
        camera.beam[c]=f->position[c];camera.beamAim[c]=f->direction[c];camera.beamLight[c]=f->color[c]*f->strength;
    }
    camera.beam[3]=f->position[3];camera.beamAim[3]=f->direction[3];
    // How thick the air hangs for it out in the open, a unit of the way; the
    // fog bank's fog and dry ice light up in the bank itself instead.
    float wind=windAmount();
    switch(kind) {
    case WEATHER_SNOW: camera.beamLight[3]=0.003f*fallNow*(0.4f+0.6f*wind);break;
    case WEATHER_SAND: camera.beamLight[3]=0.0025f*fallNow*(0.3f+0.7f*wind);break;
    case WEATHER_ASH: camera.beamLight[3]=0.0015f*fallNow;break;
    default: break;
    }
}
} // namespace doom3d
