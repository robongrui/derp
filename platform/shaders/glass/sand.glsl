// Sand on the glass (glass.frag): dry, so nothing sticks to it. As hard as
// the wind presses it onto this pane, grains strike it and drop straight
// down, fast, a texel at a time; a drift piles up against the sill, ragged,
// up to seven texels; and the pane's lower part goes blasted, a scatter of
// pale texels where the grains fly thickest. Out of the wind only some
// grit in the frame's lower corners. In the sand's colour (c.settleColor).
// x and y count texels from the pane's first end and its sill.
void sandGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float t=floor(c.fog.w*35.0)/35.0,amount=wet*press;
    vec3 sand=saturate(c.settleColor.rgb*min(vec3(0.15)+lit*0.85,vec3(1.2)));
    float drift=floor(amount*(1.0+6.0*hash(vec2(floor(x/4.0),43),seed))*(0.8+0.4*hash(vec2(x,44),seed)));
    if(y<drift) {over(result,sand,0.9);return;}
    float corner=min(x,extent.x-1.0-x);
    if(corner<3.0&&y<3.0+drift&&hash(vec2(x,y),seed^0x7ab3c911u)<0.5*wet) {over(result,sand,0.6);return;}
    // A grain in each 4x8 cell now and then: strikes, then drops 40 texels a
    // second for a third of one (the cells above reach down into this one).
    vec2 cell=floor(vec2(x/4.0,y/8.0));
    for(int k=0;k<=2;++k) {
        vec2 from=cell+vec2(0.0,float(k));
        if(hash(from,seed^0x51a9c3e7u)>amount*0.4) continue;
        float period=0.6+0.8*hash(from,seed^0x0b4d2a11u),clock=t+hash(from,seed^0x5f1c7e3bu)*period;
        float cycle=floor(clock/period),age=clock-cycle*period;
        if(age>0.35) continue;
        vec2 at=from*vec2(4,8)+floor(vec2(hash(from+cycle*13.0,seed^0x4cf5ad43u)*4.0,hash(from+cycle*17.0,seed^0x7a2b3c4du)*8.0));
        if(x==at.x&&y==at.y-floor(age*40.0)) {over(result,saturate(sand*1.25),0.85);return;}
    }
    float blasted=amount*0.3*(1.0-smoothstep(0.0,extent.y*0.6,y));
    if(hash(vec2(x,y),seed^0x2d9e6b1fu)<blasted) over(result,saturate(sand*1.3),0.22);
}
