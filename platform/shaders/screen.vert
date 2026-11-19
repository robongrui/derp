#version 450
// Full-screen triangle; SKY places it on the far plane.
#extension GL_GOOGLE_include_directive : require
#define VERTEX
#include "common.glsl"
#ifdef PRESENT
layout(set=0,binding=0) uniform sampler2D bloom;
layout(set=0,binding=1) uniform sampler2D scene;   // the drawn view (HDR in the scene: past white kept)
layout(location=11) flat out float vBloomKept;
layout(location=12) flat out float vHdrRoom;
// Where much of the view glows (SIGIL's rooms walled in fire and red rock) the bloom gives way, as
// the eye does, or it floods the view in one colour; lamps and screens in a room never come near.
// A grid over a fine mip level, not the last one: the mips of a size that isn't a power of two drop
// texels, so the last is a few spots of the view, and a mottled pool's blobs crossing them threw
// the bloom about as the camera moved. One number a frame, so here and not per pixel.
float viewGlow() {
    float level=max(float(textureQueryLevels(bloom))-7.0,0.0);
    vec3 sum=vec3(0);
    for(int y=0;y<8;++y)for(int x=0;x<12;++x)sum+=textureLod(bloom,(vec2(x,y)+0.5)/vec2(12,8),level).rgb;
    return max(sum.r,max(sum.g,sum.b))/96.0;
}
float bloomKept(float most) {return most>0.08?pow(0.08/most,0.75):1.0;}
// HDR: the headroom gives way sooner than the bloom, so a lamp blazes but a room on fire or a
// rocket in the face stays near paper white (no flashbang, no TV dimming itself after it).
float hdrRoom(float most) {return most>0.03?0.03/most:1.0;}
// HDR in the scene: the share of the view drawn past white, on a grid of points. A yard through
// a door may blaze; a view mostly past white (out into the sun) stays near white too.
float sceneRoom() {
    float over=0.0;
    for(int y=0;y<8;++y)for(int x=0;x<12;++x) {
        vec3 s=textureLod(scene,(vec2(x,y)+0.5)/vec2(12,8),0.0).rgb;
        over+=step(1.0,max(s.r,max(s.g,s.b)));
    }
    float share=over/96.0;
    return share>0.15?0.15/share:1.0;
}
#endif
void main() {
    uint i=uint(gl_VertexIndex);vec2 p=vec2((i<<1)&2u,i&2u);
#ifdef SKY
    float z=1.0;
#else
    float z=0.0;
#endif
    gl_Position=vec4(p*vec2(2,-2)+vec2(-1,1),z,1);
#ifdef PRESENT
    float most=viewGlow();
    vBloomKept=bloomKept(most);vHdrRoom=min(hdrRoom(most),sceneRoom());
#endif
    vUV=p;vLight=1;vDistance=0;vMode=MODE_SCREEN;vWorld=vec3(0);vTint=vec3(0);vLightMask=uvec2(0);
}
