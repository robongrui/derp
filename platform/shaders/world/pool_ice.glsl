// Part of world.frag and emission.frag (through caustics.glsl and shore.glsl).
#ifndef POOL_ICE_GLSL
#define POOL_ICE_GLSL
// Phobos: a liquid sector's ice (x, 0-1) and its glow left (y), from its wear entry
// (wear.glsl: the sector entries after c.right.w carry it, decompression.cpp poolIceCode).
vec2 poolIce(uint liquid) {
    uint base=uint(c.right.w);
    if(base==0u) return vec2(0.0,1.0);
    float code=sectorInfo[base+liquid].z,glow=floor(code*0.5);
    return vec2(code-2.0*glow,glow/15.0);
}
#endif
