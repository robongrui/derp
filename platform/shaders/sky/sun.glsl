// The sun along ray, as sky.frag paints it and sky/cloud_volume.glsl lets it
// through: x its small disc, y its wide glow, smooth, the sky being a
// photograph. Part of sky.frag.
bool hellSky();   // sky/hell.glsl, included after this file
vec2 sunSeen(vec3 ray) {
    float a=acos(clamp(dot(ray,sunDir()),-1.0,1.0));
    float disc=1.0-smoothstep(0.016,0.024,a),glow=exp(-a*a/0.012)*0.45+exp(-a/0.35)*0.12;
    if(c.skyLayers.w>0.0&&(hellSky()||redClouds())) disc=0.0;   // no white disc over hell (sky_hell.cpp): only its glow
    if(c.skyLayers.w>0.0&&hellSky()) glow*=0.15;   // and over its smoke hardly that: it paled the lid
    if(c.skyLayers.w>0.0&&redClouds()) glow*=0.4;   // a low one blew a red sky out to pale pink
    if(c.skyLayers.w>0.0&&citySky()) {disc=0.0;glow*=0.15;}   // no sun through a burning city's pall
    return vec2(disc,glow);
}
