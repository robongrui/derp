#version 450
// The settings panel, the loading screen and the readouts, drawn straight into the swapchain:
// an image as it is, encoded for the display (hdr_output.glsl). Nothing on them rises over white.
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
layout(set=2,binding=0) uniform sampler2D image;
layout(std140,set=3,binding=0) uniform Output { vec4 hdr; };
layout(location=0) out vec4 outColor;
#include "hdr_output.glsl"
void main() {
    vec4 color=textureLod(image,vUV,0.0);
    outColor=vec4(encodeOutput(color.rgb,vec3(0),hdr),color.a);
}
