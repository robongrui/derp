#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
// Shadows use only WAD alpha coverage, independent of palette and sprite upscale.
// DECAL: bullet marks reuse the puff sprite's coverage as a dark, fading stain;
// flashlight silhouettes reuse a thing's sprite.
layout(set=2,binding=0) uniform sampler2D image;
layout(std140,set=3,binding=0) uniform Camera CAMERA_BLOCK c;
layout(location=0) out vec4 outColor;
#include "whiteout.glsl"
#include "fog_bank.glsl"
// Shadows and marks only darken what is behind them, drawn after the world
// has had the weather laid over it: they fade by as much as the air in front
// hides the surface, the fog bank (as seen from the eye's bank) and a
// blizzard's whiteout (out under the sky in full, else as much as is round
// the eye), or they stood sharp and black in the fog.
float shownThroughWeather() {
    float shown=1.0-whiteout(vWorld,(vMode&MODE_DECAL_OUTSIDE)!=0u);
    if(c.bank.w>0.0) shown*=bankNear(vWorld).a;
    return shown;
}
float shadowCoverage(vec2 uv) {
    vec2 size=vec2(textureSize(image,0));
    if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,size))) return 0.0;
    return textureLod(image,uv/size,0.0).g;
}
void main() {
#ifdef DECAL
    float alpha=shadowCoverage(vUV);
    // MODE_HARD_SILHOUETTE: hard-edged silhouettes keep the sprite's pixel outline.
    // MODE_STIPPLED: as the flashlight's other shadows (flashStipple in
    // lighting.glsl), the edge breaks into a 2x2 ordered dither in 4 steps.
    // Coverage spread a texel each way is held against the pattern, laid on
    // the sprite's own texels: the stipple matches the outline's pixel steps,
    // moves with the shadow so it never crawls, and widens with the shadow's
    // enlargement, the farther the wall behind the thing.
    if((vMode&MODE_STIPPLED)!=0u&&(vMode&MODE_HARD_SILHOUETTE)!=0u) {
        float spread=alpha*0.4+0.15*(shadowCoverage(vUV+vec2(1,0))+shadowCoverage(vUV-vec2(1,0))+
                                     shadowCoverage(vUV+vec2(0,1))+shadowCoverage(vUV-vec2(0,1)));
        ivec2 q=ivec2(floor(vUV))&1;
        alpha=step((float(2*(q.x^q.y)+q.y)+0.5)/4.0,spread);
    } else if((vMode&MODE_HARD_SILHOUETTE)!=0u) alpha=step(0.5,alpha);
    alpha*=shownThroughWeather();
    if(alpha<0.02) discard;
    outColor=vec4(0,0,0,alpha*vLight);
#else
    float softness=1.0;
    float alpha=shadowCoverage(vUV)*0.4;
    alpha+=shadowCoverage(vUV+vec2(softness,0))*0.15;
    alpha+=shadowCoverage(vUV-vec2(softness,0))*0.15;
    alpha+=shadowCoverage(vUV+vec2(0,softness))*0.15;
    alpha+=shadowCoverage(vUV-vec2(0,softness))*0.15;
    float along=clamp(vUV.y/float(textureSize(image,0).y),0.0,1.0);
    outColor=vec4(0,0,0,alpha*vLight*(0.35+0.65*along)*shownThroughWeather());
#endif
}
