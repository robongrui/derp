#version 450
// Full-screen triangle; SKY places it on the far plane.
#extension GL_GOOGLE_include_directive : require
#define VERTEX
#include "common.glsl"
#ifdef PRESENT
layout(set=0,binding=0) uniform sampler2D bloom;
layout(location=11) flat out float vBloomKept;
// Where much of the view glows (SIGIL's rooms walled in fire and red rock) the bloom gives way, as
// the eye does, or it floods the view in one colour; lamps and screens in a room never come near.
// A grid over a fine mip level, not the last one: the mips of a size that isn't a power of two drop
// texels, so the last is a few spots of the view, and a mottled pool's blobs crossing them threw
// the bloom about as the camera moved. One number a frame, so here and not per pixel.
float bloomKept() {
    float level=max(float(textureQueryLevels(bloom))-7.0,0.0);
    vec3 sum=vec3(0);
    for(int y=0;y<8;++y)for(int x=0;x<12;++x)sum+=textureLod(bloom,(vec2(x,y)+0.5)/vec2(12,8),level).rgb;
    float most=max(sum.r,max(sum.g,sum.b))/96.0;
    return most>0.08?pow(0.08/most,0.75):1.0;
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
    vBloomKept=bloomKept();
#endif
    vUV=p;vLight=1;vDistance=0;vMode=MODE_SCREEN;vWorld=vec3(0);vTint=vec3(0);vLightMask=uvec2(0);
}
