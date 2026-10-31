// Rain on the glass (glass.frag): the drops (drops.glsl) as wet as the
// windows are, the more the harder the wind presses the rain onto this
// pane, lit like the glass, bright, with dark lenses under them.
void rainGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    float amount=wet*(0.2+0.8*press);   // a little even out of the wind, as the gusts swirl
    float drop=dropsOnGlass(texel,extent,flipped,seed,amount,false);
    if(drop>0.0) over(result,saturate(vec3(0.82,0.92,1.0)*min(vec3(0.2)+lit*0.8,vec3(1.2))),drop*(0.3+0.25*grazing));
    else if(drop<0.0) over(result,vec3(0.04,0.05,0.06)*light,0.35);
}
