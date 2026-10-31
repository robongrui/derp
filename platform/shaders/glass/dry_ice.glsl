// Dry ice on the glass (glass.frag): it is dry, so the pane stays clear,
// but where the curtain of it climbing the wall outside reaches the pane's
// bottom (touch, windows.cpp: 0 none, 1 full) rime grows in a ragged band
// from the sill, solid low and broken into needles above, up to eight
// texels.
void dryIceGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float touch,vec3 lit,float light,float grazing,float edge) {
    if(touch<=0.0) return;
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float band=wet*touch*(3.0+5.0*hash(vec2(floor(x/4.0),47),seed));
    vec3 rime=saturate(vec3(0.86,0.92,0.97)*min(vec3(0.2)+lit*0.8,vec3(1.2)));
    if(y<band*0.6) over(result,rime,0.55);
    else if(y<band&&hash(vec2(floor((x+y)*0.5),floor((x-y)/3.0)),seed^0x1f83d9abu)<0.55) over(result,rime,0.3);
}
