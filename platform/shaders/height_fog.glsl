// The height fog's extinction along a ray, shared by the world, the sky and glass (lighting.glsl)
// and the particles (flat.frag). The mist has a simpler one of its own (mist.frag).
// Fog: analytic exponential height extinction, an analytic headlamp glow, and
// one soft ray/sphere glow per nearby light. No marching through the volume.
// Use the denser endpoint for downward rays to avoid an overflowing exp(-x).
// Density thickens exponentially below the median floor, but stops 96 units
// down: deep shafts (Freedoom has some hundreds of units deep) would
// otherwise fog solid. Below that the ray sees the capped density.
float heightFogTau(float oz,float pz,float distance,float density,float falloff,float reference) {
    float lowest=reference-96.0,lo=min(oz,pz),hi=max(oz,pz);
    float below=hi-lo<1e-3?(lo<lowest?1.0:0.0):clamp((lowest-lo)/(hi-lo),0.0,1.0);
    float start=max(lo,lowest),x=falloff*(max(hi,lowest)-start);
    float f=x<1e-3?1.0-x*0.5+x*x/6.0:(1.0-exp(-x))/x;
    return density*distance*(below*exp(falloff*96.0)+
        (1.0-below)*exp(clamp(-falloff*(start-reference),-80.0,80.0))*f);
}
