/* F4's cached sky (settings.skyCache; the real-time sky stays the standard):
   the sky's costly part, everything up to its god rays, painted into a small
   cube a face at a time instead of per screen pixel per frame. sky.frag then
   only looks it up and lays over it what has to stay live: the weather's far
   sheets, the flashlight, fog, the eye's exposure.

   A face a tic, the ones the eye can see first: clouds step along at a few
   updates a second, which is what makes it look less perfect as well as cost
   less. The eye's place goes in with each face; the towers stand so far off
   that faces painted a few tics apart still meet. */
#include "internal.h"

extern "C" int gametic;

namespace gpu3d {
namespace {
SDL_GPUTexture *cube;
int cubeSize=0;
// The rule for what a face holds: its axis and where its s and t run.
struct Face { float forward[3],s[3],t[3]; };
const Face faces[6]={{{1,0,0},{0,0,-1},{0,-1,0}},{{-1,0,0},{0,0,1},{0,-1,0}},{{0,1,0},{1,0,0},{0,0,1}},
                     {{0,-1,0},{1,0,0},{0,0,-1}},{{0,0,1},{1,0,0},{0,-1,0}},{{0,0,-1},{-1,0,0},{0,-1,0}}};
int paintedTic[6];
int lastTic=-1;
float lastEye[3];
// What changes the sky all at once: a new level, sky, hour or weather. What
// drifts (cloud, a fading sun) comes along with the faces.
struct Key { const void *images[3];float place[3],cloud[4],sun[3],kind,night,shower; bool operator==(const Key &o) const {return !memcmp(this,&o,sizeof(Key));} };
Key lastKey;
Key keyOf(const Uniforms &c) {
    Key k={};
    k.images[0]=gpu(doom3d::skyLayersImage());k.images[1]=gpu(doom3d::skyLayerLight());k.images[2]=gpu(doom3d::skyCloudField());
    for(int i=0;i<3;++i)k.place[i]=c.skyLayers[i];
    for(int i=0;i<4;++i)k.cloud[i]=c.skyCloud[i];
    k.sun[0]=c.fx2[2];k.sun[1]=c.outdoor[3];k.sun[2]=c.skyWind[3];k.kind=c.weather[3];k.night=c.skyState[1];k.shower=c.skyState[3];
    return k;
}
void paintFace(SDL_GPUCommandBuffer *render,const Uniforms &camera,int face) {
    const Face &f=faces[face];
    Uniforms view=camera;
    for(int i=0;i<3;++i) {view.forward[i]=f.forward[i];view.right[i]=f.s[i];view.up[i]=-f.t[i];}
    view.projection[0]=view.projection[1]=1;
    SDL_GPUColorTargetInfo color={};
    color.texture=cube;color.layer_or_depth_plane=(Uint32)face;
    color.load_op=SDL_GPU_LOADOP_DONT_CARE;color.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(render,&color,1,nullptr);
    SDL_BindGPUGraphicsPipeline(pass,skyBakePipeline);
    SDL_GPUTextureSamplerBinding images[6];skyImages(images);
    SDL_BindGPUFragmentSamplers(pass,0,images,6);
    pushWorldUniforms(render,view);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
    SDL_EndGPURenderPass(pass);
    paintedTic[face]=gametic;
}
// The face that has waited longest, what the eye can see counting for more
// and the void under the map hardly at all.
int nextFace(const Uniforms &camera) {
    int best=0;float most=-1;
    for(int i=0;i<6;++i) {
        const float *a=faces[i].forward;
        float seen=a[0]*camera.forward[0]+a[1]*camera.forward[1]+a[2]*camera.forward[2];
        float score=(float)(gametic-paintedTic[i]+1)*(seen>-0.2f?4.0f:1.0f)*(i==5?0.25f:1.0f);
        if(score>most){most=score;best=i;}
    }
    return best;
}
} // namespace

bool skyCacheOn(const Uniforms &camera) {return settings.skyCache&&camera.skyLayers[3]>0&&cube;}
SDL_GPUTextureSamplerBinding skyCacheImage() {return {cube,settings.skyCache==2?nearestClamp:linearClamp};}

bool drawSkyCache(SDL_GPUCommandBuffer *render,const Uniforms &camera) {
    if(!settings.skyCache||camera.skyLayers[3]<=0||!worldPending||!skyBakePipeline) {lastTic=-1;return false;}
    int size=settings.skyCache==2?320:640;   // chunky: about DOOM's own pixels at its field of view
    bool all=lastTic<0;
    if(size!=cubeSize) {
        releaseSkyCache();
        cube=createTexture(SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,size,size,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,
                           SDL_GPU_SAMPLECOUNT_1,SDL_GPU_TEXTURETYPE_CUBE,6);
        if(!cube)return false;   // the real-time sky goes on
        cubeSize=size;all=true;
    }
    Key key=keyOf(camera);
    if(!(key==lastKey))all=true;
    lastKey=key;
    float moved=0;
    for(int i=0;i<3;++i)moved+=(camera.eye[i]-lastEye[i])*(camera.eye[i]-lastEye[i]);
    if(moved>256*256)all=true;   // a teleport: the towers shift at once, not face by face
    for(int i=0;i<3;++i)lastEye[i]=camera.eye[i];
    bool painted=all||gametic!=lastTic;
    if(all)for(int i=0;i<6;++i)paintFace(render,camera,i);
    else if(gametic!=lastTic)paintFace(render,camera,nextFace(camera));
    lastTic=gametic;
    return painted;
}
void releaseSkyCache() {release(cube);cubeSize=0;lastTic=-1;}
} // namespace gpu3d
