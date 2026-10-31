// Ash on the glass (glass.frag): light and a little sticky. Flakes land
// and stay, a texel each, a few of them sliding slowly down after a
// while; grey gathers on the sill and along the frame's lower lip; now and
// then an ember strikes the pane, flares orange and leaves a black cinder
// dot. The more the wind presses it onto this pane, the more of it, but
// out of the wind it still dusts the glass a little (it swirls). In the
// ash's colour (c.settleColor). x and y count texels from the pane's
// first end and its sill.
void ashGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float t=floor(c.fog.w*35.0)/35.0,amount=wet*(0.3+0.7*press);
    vec3 ash=saturate(c.settleColor.rgb*min(vec3(0.15)+lit*0.85,vec3(1.2)));
    if(y<1.0+floor(hash(vec2(floor(x/3.0),45),seed)*2.0*amount)) {over(result,ash,0.85);return;}
    if(edge<1.5&&y<extent.y*0.35&&hash(vec2(x,y),seed^0x3e5b0c4du)<amount*0.6) {over(result,ash,0.6);return;}
    // Embers: in a few 9x9 cells now and then one strikes, glows for a third
    // of a second, and a cinder stays where it struck.
    vec2 big=floor(vec2(x/9.0,y/9.0));
    if(hash(big,seed^0x6c8e9cf5u)<amount*press*0.08) {
        float period=6.0+6.0*hash(big,seed^0x1d2c3b4au),clock=t+hash(big,seed^0x2b3c4d5eu)*period;
        float cycle=floor(clock/period),age=clock-cycle*period;
        vec2 at=big*9.0+floor(vec2(hash(big+cycle*13.0,seed^0x4cf5ad43u),hash(big+cycle*17.0,seed^0x7a2b3c4du))*9.0);
        if(at==vec2(x,y)) {
            if(age<0.35) {over(result,mix(vec3(1.0,0.48,0.1),vec3(1.0,0.8,0.38),1.0-age/0.35),0.95);return;}
            over(result,vec3(0.03,0.025,0.02),0.85);return;
        }
    }
    // Flakes in 5x5 cells: land, stay most of their while; a quarter slide
    // down two to four texels a second over the second half of it (the
    // cells above reach down into this one).
    vec2 cell=floor(vec2(x/5.0,y/5.0));
    for(int k=0;k<=3;++k) {
        vec2 from=cell+vec2(0.0,float(k));
        if(hash(from,seed^0x5be0cd19u)>amount*0.5) continue;
        float period=6.0+6.0*hash(from,seed^0x510e527fu),clock=t+hash(from,seed^0x9b05688cu)*period;
        float cycle=floor(clock/period),age=clock-cycle*period;
        if(age>period*0.9) continue;
        vec2 at=from*5.0+floor(vec2(hash(from+cycle*13.0,seed^0x4cf5ad43u),hash(from+cycle*17.0,seed^0x7a2b3c4du))*5.0);
        if(hash(from+cycle*19.0,seed^0x68bc21ebu)<0.25) at.y-=floor(max(age-period*0.5,0.0)*(2.0+2.0*hash(from,seed^0x7feb352du)));
        if(at==vec2(x,y)) {over(result,ash,0.85);return;}
    }
    if(hash(vec2(x,y),seed^0x2c1b3c6du)<amount*0.12) over(result,ash,0.35);
}
