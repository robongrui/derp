// A low sun rakes across the deck. High up, a cloud's shadow falls on what lies
// under it; low down it runs on for many times the cloud's own thickness, over
// the tops of the clouds beyond, and the deck is lit only in the long lanes the
// gaps toward the sun let through. sky/cloud_volume.glsl's few light steps reach
// a couple of thicknesses and could not know this. Part of sky.frag.
// How much it counts: nothing at any height the deck was already lit for.
float rakeStrength() {return citySky()||hellSky()?0.0:lowSun();}
// How much of the sun reaches the deck at q, 0 in a long shadow to 1 in a lane
// of light: a walk toward the sun as far as a shadow at this height reaches.
// Even steps: gathered near q it only read the gap at q, and lit blobs, not lanes.
// Coarse by nature: only the field and its gaps.
float sunRake(vec2 q) {
    vec3 s=sunDir();
    vec2 way=normalize(s.xy);
    float reach=c.skyShape.x*min(0.3/max(s.z,0.05),3.0),open=0.0;
    for(int i=1;i<=12;++i)open+=1.0-fieldBlock(q+way*reach*float(i)/12.0);
    return smoothstep(0.05,0.8,open/12.0);
}
// 1 looking toward the sun, round the horizon, to 0 with it behind you.
float sunSide(vec3 ray) {return 0.5+0.5*dot(normalize(ray.xy),normalize(sunDir().xy));}
