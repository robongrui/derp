#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
// The exposed picture (the shrink only): what burns out over the tone curve's shoulder glows
// too, by direction.x, so a yard seen from a dark room spills over its doorway's edges, as
// Lost Coast's bloom from the finished frame does. 0: off (Classic, where Doom's full-bright
// rooms sit at white and would glow everywhere).
layout(set=2,binding=1) uniform sampler2D scene;
// direction.xy: one blur step along an axis, a texel of the image apart (the weights below are made for
// that: spread wider they leave gaps, and the blur is a few copies of the image). direction.zw, when
// set: no blur, the image shrunk instead, zw the size of one texel of the target; 4x4 taps spread over
// it count every texel under it (up to 8 a side), so a thin far lamp strip neither drops out nor flickers.
layout(std140,set=3,binding=0) uniform Direction { vec4 direction; };
layout(location=0) out vec4 outColor;
void main() {
    vec3 glow=vec3(0);
    if(direction.z>0.0) {
        for(int y=0;y<4;++y)for(int x=0;x<4;++x) {
            vec2 at=vUV+(vec2(x,y)-1.5)*0.25*direction.zw;
            glow+=textureLod(image,at,0.0).rgb;
            if(direction.x>0.0) {
                // From the shoulder's last stretch (scene 1.0 lands at 0.89) to white.
                vec3 shown=textureLod(scene,at,0.0).rgb;
                glow+=shown*smoothstep(0.86,1.0,max(shown.r,max(shown.g,shown.b)))*direction.x;
            }
        }
        outColor=vec4(glow/16.0,1);
        return;
    }
    // Both steps set: across and down in one pass, the same taps' product (5x5 taps), so a blur
    // that was two passes is one. On a Steam Deck each pass cost about 0.1 ms whatever it drew.
    if(direction.x>0.0&&direction.y>0.0) {
        const float at[3]=float[3](0.0,1.384615,3.230769),weight[3]=float[3](0.227027,0.316216,0.070270);
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)
            glow+=textureLod(image,vUV+vec2(float(sign(x))*at[abs(x)]*direction.x,float(sign(y))*at[abs(y)]*direction.y),0.0).rgb*
                  weight[abs(x)]*weight[abs(y)];
        outColor=vec4(glow,1);
        return;
    }
    glow=textureLod(image,vUV,0.0).rgb*0.227027;
    glow+=(textureLod(image,vUV+direction.xy*1.384615,0.0).rgb+
           textureLod(image,vUV-direction.xy*1.384615,0.0).rgb)*0.316216;
    glow+=(textureLod(image,vUV+direction.xy*3.230769,0.0).rgb+
           textureLod(image,vUV-direction.xy*3.230769,0.0).rgb)*0.070270;
    outColor=vec4(glow,1);
}
