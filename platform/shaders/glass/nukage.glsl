// Nukage rain on the glass (glass.frag): rain's drops (drops.glsl) in its
// own colour (c.weatherTint), as much as the wind presses it onto this
// pane, and each runner leaves a faint green track all the way down where
// it went, until the next one runs.
void nukageGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    float amount=wet*(0.2+0.8*press);
    float drop=dropsOnGlass(texel,extent,flipped,seed,amount,true);
    vec3 tint=saturate(c.weatherTint.rgb*1.2*min(vec3(0.2)+lit*0.8,vec3(1.2)));
    if(drop==0.25) over(result,tint,0.14);
    else if(drop>0.0) over(result,tint,drop*(0.3+0.25*grazing));
    else if(drop<0.0) over(result,vec3(0.04,0.05,0.06)*light,0.35);
}
