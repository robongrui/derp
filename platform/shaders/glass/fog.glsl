// Fog on the glass (glass.frag): it mists the pane over, a milky film
// thicker toward the frame in the fog's colour as lit there, and the damp
// gathers on the cold glass as fine beads that now and then run
// (drops.glsl), faint, without the dark lens rain's make. Facing the wind
// or not alike: the fog lies, it isn't blown.
void fogGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    over(result,saturate(c.settleColor.rgb*min(vec3(0.2)+lit*0.8,vec3(1.2))),wet*(0.18+0.22*(1.0-smoothstep(0.0,16.0,edge))));
    float drop=dropsOnGlass(texel,extent,flipped,seed^0x3a7e91c5u,wet*0.6,false);
    if(drop>0.0) over(result,saturate(vec3(0.85,0.9,0.92)*min(vec3(0.2)+lit*0.8,vec3(1.2))),drop*0.22);
}
