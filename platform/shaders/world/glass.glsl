// Monitor glass in computer textures: where a texel sits on its screen and
// the curved CRT dome over it.
// Part of world.frag, which declares the resources these read.
bool isGlass(vec2 uv) {
    return texelFetch(image,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0).a<0.999;
}
// Position across the screen in [-1,1], and the screen's top-left texel and
// size in texels.
vec2 glassScreen(vec2 uv,out vec2 origin,out vec2 extent) {
    vec4 f=texelFetch(glassFrame,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0)*255.0;
    origin=floor(uv)-round(f.rg);extent=max(round(f.ba),vec2(1));
    return clamp((uv-origin)/extent*2.0-1.0,vec2(-1),vec2(1));
}
// The CRT dome, peaking at a quarter of the shorter side: its smooth normal.
vec3 glassDome(vec2 screen,vec2 extent,vec3 normal,vec3 tangent,vec3 bitangent) {
    float peak=min(extent.x,extent.y)*0.25;
    vec2 slope=-peak*4.0*screen*(1.0-screen.yx*screen.yx)/extent;
    return normalize(normal-(slope.x*tangent+slope.y*bitangent));
}
