// Sky depth's god rays (sky/clouds.glsl, sky/depth.glsl): shafts of the
// sun's light fanning out past the towers' silhouettes and through the gaps
// in the cloud deck. From each direction a dozen steps toward
// the sun ask whether a painted crest or the deck stands in the way; the
// share that sees the sun lights the haze along the way, so gaps become
// shafts and towers and thick cloud cut dark ones. Only in the sky, only
// when the weather makes them: strongest in broken cloud with a low sun,
// faint in clear air, none under full overcast or fog (no sun to cast
// them), and none with the sun itself behind the mountains (c.skyState.z,
// sky_sun_block.cpp): nothing to cast them. Smooth, moving with the deck.
float rayStrength() {
    float cover=cloudCover(),broken=saturate(1.0-abs(cover-0.55)/0.35);
    if(fieldSky())broken=sunBreaks();   // the shafts are what comes through its gaps
    return (0.35+0.65*broken)*mix(0.5,1.0,lowSun())*saturate(sunOn()*2.0)*saturate(c.fx2.w*3.0)*c.skyState.z;
}
// How much the way along d is blocked from the reference eye: the painted
// crest of the near or far towers in its direction, else the deck.
float blockedAlong(vec3 d,ivec2 size) {
    int x=wrapColumn(int(floor(-atan(d.y,d.x)/6.2831853*float(size.x)*4.0)),size.x);
    float rise=d.z/max(length(d.xy),1e-4),crest=paintedRise(x,0,size);
    if(c.skyExtent.z>0.0)crest=max(crest,paintedRise(x,1,size));
    if(rise<crest)return 1.0;
    if(d.z<=0.02)return 0.0;
    vec3 o=highEye();
    vec2 q=o.xy+d.xy*(cloudDeck*c.skyShape.x-o.z)/d.z;
    return fieldSky()?fieldBlock(q):deckDensity(q);
}
vec3 godRays(vec3 color,vec3 ray,ivec2 size) {
    float strength=rayStrength();
    vec3 s=sunDir();
    float toward=dot(ray,s);
    if(strength<=0.0||toward<0.0)return color;
    float lit=0.0;
    for(int i=0;i<12;++i)lit+=1.0-blockedAlong(normalize(mix(ray,s,(float(i)+0.5)/12.0)),size);
    // Shafts, not a glow: where the way to the sun is open the haze lights
    // up, where a tower or thick cloud blocks it the air goes dim.
    float fan=toward*toward*strength;
    lit/=12.0;
    return color*(1.0-0.35*fan*(1.0-lit))+c.sun.rgb*lit*fan*0.6;
}
