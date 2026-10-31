// Frost creeping in over the glass (glass.frag; snow.glsl in a room the
// cold has got into), the same from both sides: from the frame a texel at
// a time, farther in the corners and up from the sill, in blocks of four
// texels each reaching its own way; solid near the frame, broken further
// in into needles slanting across the pane. x and y count texels from the
// pane's first end and its sill; 1 frost, 0.5 a needle, 0 clear.
float frostCreep(float x,float y,vec2 extent,uint seed,float amount) {
    if(amount<=0.0) return 0.0;
    float side=min(x,extent.x-1.0-x),upright=min(y*0.7,extent.y-1.0-y);
    float d=min(side,upright)*mix(0.5,1.0,smoothstep(0.0,14.0,max(side,upright)));
    float reach=amount*(3.0+6.0*hash(floor(vec2(x,y)/4.0),seed^0x3b9ac9ffu));
    if(d<reach*0.6) return hash(vec2(x,y),seed^0x6a09e667u)<0.1?0.5:1.0;
    if(d<reach&&hash(vec2(floor((x+y)*0.5),floor((x-y)/3.0)),seed^0x1f83d9abu)<0.55*(1.0-(d-reach*0.6)/(reach*0.4))) return 0.5;
    return 0.0;
}
