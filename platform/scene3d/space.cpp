/* Physically accurate Phobos (Game_Phobos): the sky is space. No painted sky
   at all, mountains included: sky.frag draws black space with stars and
   Mars, and whatever else reads the sky texture (window glass's
   reflections) gets one texel with no coverage, so it sees black too. */
#include "internal.h"

namespace doom3d {
static GpuTextureRef spaceTexture;

const GpuTextureRef &skyImage() {
    if(!Game_Phobos())return wallImage(skytexture).texture;
    if(!spaceTexture) {
        const byte empty[2]={0,0};
        spaceTexture=gpuCreateTexture(GpuFormat::RG8,1,1,empty,2);
    }
    return spaceTexture?spaceTexture:wallImage(skytexture).texture;
}
void spaceShutdown() {spaceTexture=nullptr;}
} // namespace doom3d
