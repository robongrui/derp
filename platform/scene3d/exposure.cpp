/* Eye adaptation to the light around the player, and the flashlight
   tint and bounce from what it lights. */
#include "internal.h"

namespace doom3d {
static std::array<float,3> flashlightTint={},flashlightTargetTint={};
static uint64_t flashlightSampleTime;
static uint64_t flashlightTime;
static unsigned flashlightLevelSerial;
// The flashlight's bounce: a soft light where the beam lands, in the color of
// what it lights, so a bright spot on a wall spills onto the dark floor
// before it rather than standing beside it untouched.
struct Bounce {std::array<float,3> position,color;float normal[2],strength;};
static Bounce bounce={},targetBounce={};

// Eye adaptation (F4, off unless the player asks for it): the picture's
// exposure follows the light around and ahead of the player, as an eye or a
// camera does. In a dim room it opens up, so the daylight a window lets in
// lights the room and the window itself burns out; out in the sun it closes
// down, and a doorway seen from the yard is dark. Steady, not a passing
// flash: it holds for as long as the light does, and it moves smoothly, by
// the clock and not by the tic, quickly toward the brighter and slowly toward
// the darker, as eyes do. The light is read off the picture itself, as a
// camera meters (gpu3d/frame.cpp's thumbnail of the 3D view): by the light
// in the level alone a lamp-lit room measured nearly as bright as a yard,
// when Doom's dark textures make it look a quarter of that. The player's own
// shots it holds still through (shotHold); the status bar is not in the view.
// With whole-level lighting daylight is then drawn at twice the level's
// light (daylightScale: the sky's light in the bounce bake, the sun, the sky
// itself), and plain, in proportion to the sky a place sees: out there the
// picture closes down to about what it is with the switch off, and from a
// room the yard through the door burns out, over the tone curve's shoulder
// (common.glsl's toneCurve). The eye does what a baked curve had to fake.
static float adaptedLight=-1,adaptedBright=-1,exposure=1;static uint64_t adaptTime=0,shotHold=0;static unsigned adaptLevelSerial;
// The light the picture is exposed for, as the frame's meter reads it (gpuFrameLight: the
// geometric mean of the picture's luminance), and how far the exposure follows it: seven
// tenths of the way in stops, since an eye adapts but does not make every place equally
// bright. E1M1's lamp-lit start room reads 0.084 and opens to about 1.5; E1M6's court at noon,
// drawn at twice the level's light, closes to about 0.55.
constexpr float exposureLight=0.15f,exposureFollows=0.7f;
constexpr float exposureLeast=0.4f,exposureMost=1.7f;   // the dark stays dark: no more than this is made up for
constexpr float exposureBright=0.8f;   // and the bright end of the picture is held to this (updateExposure)
float daylightScale() {return settings.eyeAdaptation&&settings.lightModel==1&&settings.skyLight&&settings.bounce?2.0f:1.0f;}
void updateExposure(Uniforms &camera) {
    if(!settings.eyeAdaptation||camera.effects[2]!=0||!players[displayplayer].mo) {adaptedLight=-1;exposure=1;return;}
    uint64_t now=SDL_GetTicksNS();
    if(adaptLevelSerial!=r_levelserial) {adaptedLight=adaptedBright=-1;exposure=1;shotHold=0;adaptLevelSerial=r_levelserial;}
    // The picture was drawn at the exposure of its frame; what it shows of the scene is that taken out again.
    float frame=gpuFrameLight();
    if(frame<0) {camera.fx[0]=exposure;return;}
    float light=std::max(frame/std::max(exposure,0.1f),0.005f);
    // The bright end of the scene, as the picture's is with its exposure and the tone curve taken out again.
    float shown=std::clamp(gpuFrameBright(),0.0f,0.995f);
    if(daylightScale()>1&&shown>0.7f)shown=0.7f-0.3f*std::log(1-(shown-0.7f)/0.3f);
    float bright=std::max(shown/std::max(exposure,0.1f),0.02f);
    // The player's own shot is too brief for the eye: it holds still while the flash lights the
    // view and a little after, the meter reading a frame or two behind. Following it, the eye
    // closed for every shot and held closed under the chaingun, and the flash put itself out.
    if(ownFlashLit()) shotHold=now+150000000ull;
    if(adaptedLight<0) {adaptedLight=light;adaptedBright=bright;}
    else if(now>=shotHold) {
        float dt=std::min((now-adaptTime)/1e9f,0.25f),tau=light>adaptedLight?0.35f:1.5f;
        adaptedLight+=(light-adaptedLight)*(1-std::exp(-dt/tau));
        adaptedBright+=(bright-adaptedBright)*(1-std::exp(-dt/(bright>adaptedBright?0.35f:1.5f)));
    }
    adaptTime=now;
    exposure=std::clamp(std::pow(exposureLight/adaptedLight,exposureFollows),exposureLeast,exposureMost);
    // Opened up for a dim room, the picture burned out wherever the room was lit: its bright
    // end is held under white. Never below what the middle alone would close down to in daylight.
    exposure=std::max(std::min(exposure,exposureBright/adaptedBright),exposureLeast);
    camera.fx[0]=exposure;
#ifdef DOOM_DEVTOOLS
    // DERP_EXPOSURE: prints the meter once a second.
    static int said=-1;
    if(getenv("DERP_EXPOSURE")&&leveltime/35!=said) {said=leveltime/35;fprintf(stderr,"exposure: frame %.3f scene %.3f settled %.3f exposure %.2f\n",frame,light,adaptedLight,exposure);}
#endif
}
// Eyes settled to sunlit ground see no stars; settled to a dim room or a
// shaded corner they come out. Without eye adaptation, a faint few.
float starVisibility() {
    if(adaptedLight<0)return 0.35f;
    float t=std::clamp((exposureLight*1.2f-adaptedLight)/(exposureLight*0.6f),0.0f,1.0f);
    return t*t*(3-2*t);
}
// One target-color ray at 30 Hz supplies a smooth, subtle ambient tint.
// It creates no virtual lights, portal blocker lists, or per-pixel bounce work.
void updateFlashlightTint(Uniforms &camera) {
    uint64_t now=SDL_GetTicksNS();
    if(flashlightLevelSerial!=r_levelserial) {
        flashlightTint={};flashlightTargetTint={};flashlightTime=0;flashlightSampleTime=0;
        bounce={};targetBounce={};flashlightLevelSerial=r_levelserial;
    }
    if(!flashlightOn||camera.effects[2]!=0) {flashlightTargetTint={};targetBounce.strength=0;}
    else if(!flashlightSampleTime||now-flashlightSampleTime>=33333333) {
        flashlightSampleTime=now;
        const byte *palette=(const byte*)W_CacheLumpName((char*)"PLAYPAL",PU_CACHE);
        using Vec=std::array<float,3>;
        auto dot=[](Vec a,Vec b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
        auto cross=[](Vec a,Vec b)->Vec{return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};};
        Vec direction;
        for(int c=0;c<3;++c) direction[c]=camera.forward[c];
        float length=std::sqrt(dot(direction,direction));for(float &c:direction)c/=length;
        float nearest=900,nearestSurface=900;Vec reflected={};Bounce hit={};
        for(const auto &batch:batches) {
            if(batch.second.empty())continue;   // a key left over from a dropped picture
            const auto &image=images.at(batch.first);const auto &vertices=batch.second;
            for(size_t i=0;i+2<vertices.size();i+=3) {
                const auto &a=vertices[i],&b=vertices[i+1],&c=vertices[i+2];
                Vec ab={b.x-a.x,b.y-a.y,b.z-a.z},ac={c.x-a.x,c.y-a.y,c.z-a.z};
                Vec h=cross(direction,ac);float det=dot(ab,h);if(std::abs(det)<0.0001f)continue;
                Vec origin={camera.eye[0]-a.x,camera.eye[1]-a.y,camera.eye[2]-a.z};
                float u=dot(origin,h)/det;if(u<0||u>1)continue;
                Vec q=cross(origin,ab);float v=dot(direction,q)/det;if(v<0||u+v>1)continue;
                // The tint takes whatever is nearest; the bounce only map
                // surfaces, so a monster in the beam isn't lit flat from a
                // light on its own face.
                bool sprite=(a.mode&MODE_SPRITE)!=0;
                float distance=dot(ac,q)/det;if(distance<1||(distance>=nearest&&(sprite||distance>=nearestSurface)))continue;
                int tx=(int)std::floor(a.u+u*(b.u-a.u)+v*(c.u-a.u));
                int ty=(int)std::floor(a.v+u*(b.v-a.v)+v*(c.v-a.v));
                if(sprite) {if(tx<0||ty<0||tx>=image.width||ty>=image.height)continue;}
                else {tx=(tx%image.width+image.width)%image.width;ty=(ty%image.height+image.height)%image.height;}
                size_t pixel=((size_t)ty*image.width+tx)*2;
                if(!image.pixels[pixel+1])continue;
                // Average a small patch of artwork so a single dark mortar
                // pixel doesn't abruptly change the tint while aiming.
                Vec albedo={};float samples=0;
                for(int py=-2;py<=2;++py)for(int px=-2;px<=2;++px) {
                    int x=(tx+px+image.width)%image.width,y=(ty+py+image.height)%image.height;
                    size_t index=((size_t)y*image.width+x)*2;
                    if(!image.pixels[index+1])continue;
                    const byte *rgb=palette+image.pixels[index]*3;
                    for(int c=0;c<3;++c)albedo[c]+=rgb[c]/255.0f;
                    ++samples;
                }
                for(float &c:albedo)c/=std::max(1.0f,samples);
                if(distance<nearest) {
                    nearest=distance;
                    float gain=0.30f*std::pow(std::max(0.0f,1-distance/900),2.0f)*settings.flashlightTintGain;
                    for(int c=0;c<3;++c)reflected[c]=albedo[c]*gain;
                }
                if(sprite||distance>=nearestSurface)continue;
                nearestSurface=distance;
                // The bounce: as bright as the beam there (its falloff in
                // collectFlashes) times how much the artwork reflects.
                Vec normal=cross(ab,ac);float n=std::sqrt(dot(normal,normal));
                if(n<0.0001f)continue;
                for(float &c:normal)c/=n;
                if(dot(normal,direction)>0)for(float &c:normal)c=-c;
                float peak=std::max({albedo[0],albedo[1],albedo[2]}),beam=std::max(0.0f,1-distance/900);
                hit.strength=peak>0?0.6f*1.9f*beam*beam*(3-2*beam)*(0.299f*albedo[0]+0.587f*albedo[1]+0.114f*albedo[2]):0;
                for(int c=0;c<3;++c) {hit.position[c]=camera.eye[c]+direction[c]*distance;hit.color[c]=peak>0?albedo[c]/peak:0;}
                // Walls: a panel 4 units out shining into the room (doom_panel_emission);
                // floors, ceilings and anything tilted: a point a little off it.
                float flat=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]);
                bool wall=std::abs(normal[2])<0.1f;
                hit.normal[0]=wall?normal[0]/flat:0;hit.normal[1]=wall?normal[1]/flat:0;
                for(int c=0;c<3;++c)hit.position[c]+=normal[c]*(wall?4.0f:16.0f);
            }
        }
        flashlightTargetTint=reflected;targetBounce=hit;
    }
    float seconds=flashlightTime?std::min(0.1f,(now-flashlightTime)/1e9f):1.0f/60;
    flashlightTime=now;float blend=1-std::exp(-seconds/0.16f);
    for(int c=0;c<3;++c) {
        flashlightTint[c]+=(flashlightTargetTint[c]-flashlightTint[c])*blend;
        camera.flashlightTint[c]=camera.effects[2]==0?flashlightTint[c]:0;
    }
    // Moving across one surface the bounce follows; jumping to another it
    // fades in there instead of sliding through the room.
    float dx=targetBounce.position[0]-bounce.position[0],dy=targetBounce.position[1]-bounce.position[1],dz=targetBounce.position[2]-bounce.position[2];
    bool jump=dx*dx+dy*dy+dz*dz>48*48||targetBounce.normal[0]!=bounce.normal[0]||targetBounce.normal[1]!=bounce.normal[1];
    if(targetBounce.strength>0&&(bounce.strength<=0.001f||jump)) {bounce=targetBounce;bounce.strength=0;}
    bounce.strength+=(targetBounce.strength-bounce.strength)*blend;
    for(int c=0;c<3;++c) {
        bounce.position[c]+=(targetBounce.position[c]-bounce.position[c])*blend;
        bounce.color[c]+=(targetBounce.color[c]-bounce.color[c])*blend;
    }
}
bool flashlightBounce(float position[3],float normal[2],std::array<float,3> &color,float &strength) {
    if(bounce.strength<0.01f) return false;
    for(int c=0;c<3;++c)position[c]=bounce.position[c];
    normal[0]=bounce.normal[0];normal[1]=bounce.normal[1];color=bounce.color;strength=bounce.strength;
    return true;
}
void exposureShutdown() {
    flashlightTint={};flashlightTargetTint={};flashlightTime=0;flashlightSampleTime=0;
    bounce={};targetBounce={};
    adaptedLight=-1;exposure=1;
}
} // namespace doom3d
