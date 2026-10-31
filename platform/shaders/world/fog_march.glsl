// The fog bank along the view ray, marched. fog_bank.glsl's bankAlong knows
// only the eye's bank and the point's, so fog lying between them (a
// courtyard seen through a window, its top over the sill) went missing and
// showed what lies under it. Here the ray goes in 12 stretches, each in the
// bank of the sector under its middle (the map's cells; where none, or one
// whose floor lies above it, the last one found; indoors dry ice's film
// there, bankFilm), the last in the point's
// own (there: its top, share and light, streams included). Each stretch's
// fog lies over what is behind it, lit by its own sector, front to back;
// what glows (cut) keeps more of itself; a blast's clearing (bankClear)
// thins it where it is open; then the bank's steps or not. Where the
// flashlight's beam crosses a stretch it lights it too (beam_air.glsl): dry
// ice glows like stage fog round the beam, fog round it in a soft cone.
// Part of world.frag, which declares the resources these read.

// Indoors dry ice lies as a film per floor cell (scene3d/floor_film.cpp),
// in the floor light's alpha (32 units to the full): a bank (top, share,
// light) over ground at that height, raised by it at a map cell.
vec3 bankFilm(vec3 bank,ivec2 cell,float ground) {
    if(c.weather.w!=WEATHER_DRY_ICE) return bank;
    float film=texelFetch(flatLight,cell,0).a*32.0;
    return film>0.0?vec3(max(bank.x,ground+film),max(bank.y,saturate(film/3.0)),bank.z):bank;
}
vec4 bankMarch(vec3 end,vec3 there,float cut) {
    if(c.bank.w<=0.0) return vec4(0,0,0,1);
    const int steps=12;
    vec3 ray=end-c.eye.xyz;
    // The point's own stretch is its last 48 units at least (a thin stream
    // across the floor there shows through no less), the rest shared out.
    float distance=length(ray),last=min(distance,max(distance/float(steps),48.0)),keep=1.0-0.6*cut,through=1.0;
    float split=distance>0.0?1.0-last/distance:0.0;
    vec3 lit=vec3(0),bank=c.bank.xyz;
    ivec2 size=textureSize(seams,0);
    for(int i=0;i<steps;++i) {
        float t0=i==steps-1?split:split*float(i)/float(steps-1),t1=i==steps-1?1.0:split*float(i+1)/float(steps-1);
        float stretch=distance*(t1-t0);
        vec3 here=bank;
        if(i==steps-1) here=there;
        else if(c.map.z>0.0) {
            vec3 p=c.eye.xyz+ray*(0.5*(t0+t1));
            ivec2 cell=clamp(ivec2(floor((p.xy-c.map.xy)*c.map.z)),ivec2(0),size-1);
            uint own=uint(round(texelFetch(seams,cell,0).x*65535.0));
            if(own!=0xFFFFu&&p.z>=sectorInfo[own].y-1.0) {
                vec4 entry=sectorInfo[uint(c.bankColor.w)+own];
                here=bankFilm(vec3(entry.x,entry.y-2.0*floor(entry.y/2.0),entry.z),cell,sectorInfo[own].y);
                if(sectorInfo[own].w>0.0) here.x+=bankWave(p.xy,sectorOpen(own));   // out under the sky: the wind's swells
                bank=here;
            }
        }
        float z0=c.eye.z+ray.z*t0,z1=c.eye.z+ray.z*t1;
        // Blown open there (a blast's clearing, fog_bank.glsl): the point's own stretch at the point.
        float clear=bankClear(i==steps-1?end.xy:c.eye.xy+ray.xy*(0.5*(t0+t1)));
        float pass=exp(-c.bank.w*stretch*here.y*clear*bankMean(z0-here.x,z1-here.x)*keep);
        vec3 beam=c.beamLight.rgb*0.6*beamAir(c.eye.xyz+ray*(0.5*(t0+t1)));
        lit+=through*(1.0-pass)*c.settleColor.rgb*(here.z+beam);
        through*=pass;
    }
    float fogged=bankFogged(-log(max(through,1e-6)));
    return vec4(lit/max(1.0-through,1e-4)*fogged,1.0-fogged);
}
