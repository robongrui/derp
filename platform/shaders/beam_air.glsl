// The flashlight's beam in the weather (scene3d/beam_air.cpp): c.beam its
// lamp and reach (w 0: off, or nothing in the air), c.beamAim its way and
// cone (as lightFalloff), c.beamLight its light, w how thick the air hangs
// for it out in the open a unit of the way (snow, sand, ash; 0 for the fog
// bank, which lights up in itself, world/fog_march.glsl). No wall test: the
// lamp rides at the eye, and what the eye sees the beam reaches. Smooth, no
// dither: it spreads over too much of the view. Expects the Camera block c.
float beamAir(vec3 p) {
    if(c.beam.w<=0.0) return 0.0;
    vec3 d=p-c.beam.xyz;
    float l=length(d),fall=max(0.0,1.0-l/c.beam.w);
    if(fall<=0.0) return 0.0;
    float cone=smoothstep(c.beamAim.w,0.985,l>0.001?dot(d/l,c.beamAim.xyz):1.0);
    return fall*fall*(3.0-2.0*fall)*cone;
}
// The glow it raises in the air along way units of view, lit the mean
// beamAir over them, each point counted as much as it is out in the open:
// the lamp's light off what hangs there (c.settleColor, its colour).
vec3 beamGlow(float way,float lit) {
    if(c.beamLight.w<=0.0||lit<=0.0) return vec3(0);
    return c.beamLight.rgb*c.settleColor.rgb*0.45*(1.0-exp(-c.beamLight.w*way*lit));
}
// Lying snow and sand glitter in the beam as the real thing does: each grain
// (2 units on the world grid along the surface) has one facet, tilted its own
// way and fixed, and flashes only while that facet lies within a few degrees
// of halfway between the lamp and the eye. So the glints hold still while
// the eye does, slide and swap over as it moves or the beam sways, gather
// where the beam is strongest, and never reshuffle at random. Bright in
// four steps. Snow: every grain an ice crystal; sand: one grain in four
// quartz, warmer.
float beamHash(uvec3 v) {
    v=v*1664525u+1013904223u;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    v^=v>>16u;
    v.x+=v.y*v.z;
    return float(v.x&0xFFFFu)/65535.0;
}
vec3 beamGlitter(vec3 p,vec3 normal) {
    float b=min(beamAir(p),1.0);
    if(b<0.05) return vec3(0);
    bool sand=c.weather.w==WEATHER_SAND;
    vec2 along=abs(normal.z)>0.5?p.xy:vec2(dot(p.xy,vec2(-normal.y,normal.x)),p.z);
    uvec2 grain=uvec2(ivec2(floor(along*0.5)));
    if(sand&&beamHash(uvec3(grain,3u))>0.25) return vec3(0);
    // Its facet: the surface's normal plus a random lean, fixed per grain.
    vec3 lean=vec3(beamHash(uvec3(grain,1u)),beamHash(uvec3(grain,2u)),beamHash(uvec3(grain,4u)))*2.0-1.0;
    vec3 facet=normalize(normal*0.7+lean);
    vec3 half_=normalize(normalize(c.beam.xyz-p)+normalize(c.eye.xyz-p));
    float glint=smoothstep(0.992,0.998,dot(facet,half_));
    glint=floor(glint*4.0+0.5)/4.0;
    if(glint<=0.0) return vec3(0);
    return c.beamLight.rgb*(sand?mix(vec3(1),c.settleColor.rgb,0.5):vec3(1))*1.1*glint*b;
}
