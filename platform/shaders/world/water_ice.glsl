// Water frozen in the cold (scene3d/water_ice.cpp): how far the water at a
// floor texel has frozen, 0 open water to 1 a white sheet, between the map's
// cells, from the ceiling light's alpha in flatLight (nothing else uses it
// there). Part of world.frag.
float waterIce(vec2 t) {
    ivec2 size=textureSize(flatLight,0),layer=max(size/ivec2(1,4),ivec2(1));
    vec2 g=(t-c.map.xy)*c.map.z-0.5;ivec2 q=ivec2(floor(g));vec2 f=fract(g);
    float sum=0.0;
    for(int n=0;n<4;++n) {
        ivec2 o=ivec2(n&1,n>>1);
        sum+=texelFetch(flatLight,clamp(q+o,ivec2(0),layer-1)+ivec2(0,layer.y),0).a*(o.x!=0?f.x:1.0-f.x)*(o.y!=0?f.y:1.0-f.y);
    }
    return sum;
}
// Its band at a texel, by the field with a ragged edge (never a straight
// line where it thins): below a third open water (-1, the ice drawn there
// discards); then floes, ragged plates growing together, a darker rim of
// slush round each (0.2, the floes 0.55); then thin dark ice clearing to the
// white sheet (0.25 to 1). How white the ice draws there.
float waterIceBand(vec2 t) {
    float v=waterIce(t)+(wearNoise(t,9.0,0x5be0cd19u)-0.5)*0.3;
    if(v<0.35) return -1.0;
    if(v<0.55) {
        float floe=wearNoise(t,6.0,0x1f83d9abu),open=1.0-(v-0.35)/0.2;
        if(floe<open) return -1.0;
        return floe<open+0.06?0.2:0.55;
    }
    return 0.25+0.75*smoothstep(0.55,0.8,v);
}
