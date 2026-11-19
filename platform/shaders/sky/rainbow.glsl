// A sunshower's rainbow (cloud_state.glsl's sunShower): round the point opposite
// the sun, red 42 degrees out and violet inside it, with the faint second bow
// past it, colours the other way, and the darker air between. It stands in the
// rain, on this side of the mountains, and needs no sun on the eye: rain out
// there is lit though a mountain hides the sun from here, so a low sun gives the
// tallest bow and a high one none. Patchy, as the rain is. Part of sky.frag.
bool hellSky();   // sky/hell.glsl, included after this file
bool skyBurns();
float bowBump(float t,float at,float width) {float d=(t-at)/width;return max(1.0-d*d,0.0);}
// t 0 violet at the inside to 1 red at the outside.
vec3 bowColour(float t) {
    return vec3(bowBump(t,0.82,0.34)+0.35*bowBump(t,0.04,0.14),bowBump(t,0.52,0.3),bowBump(t,0.2,0.28));
}
vec3 rainbowOver(vec3 color,vec3 ray,float solid) {
    if(!sunShower()||ray.z<=0.0||skyBurns()||hellSky()||citySky())return color;
    float a=acos(clamp(dot(ray,-sunDir()),-1.0,1.0));
    if(a<0.6||a>1.0)return color;
    float first=(a-0.69)/0.046,second=1.0-(a-0.885)/0.06;
    vec3 bow=bowColour(first)*smoothstep(0.0,0.12,first)*(1.0-smoothstep(0.88,1.0,first));
    bow+=0.3*bowColour(second)*smoothstep(0.0,0.15,second)*(1.0-smoothstep(0.85,1.0,second));
    float rain=0.45+0.55*smoothstep(0.32,0.62,textureLod(cloudNoise,vec3(ray.xy*1.3,0.62),0.0).r);
    float between=smoothstep(0.735,0.76,a)*(1.0-smoothstep(0.86,0.885,a));   // Alexander's dark band
    float low=smoothstep(0.0,0.12,ray.z),rock=solid<1e29?0.6:1.0;
    return color*(1.0-0.1*between*low)+bow*0.26*rain*low*rock*saturate(c.fx2.w*2.0);
}
