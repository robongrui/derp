// Drops on the glass (glass.frag), for what runs as water on it (rain,
// nukage rain, the fog's condensation; wet: how wet this pane is), in the
// same place from both sides: x and y count texels
// from the pane's first end and its sill. Beads sit in some of its 5x6
// cells, each for its own few seconds before it goes and another forms;
// in some 7-texel columns now and then one runs down, a texel a step at
// the tic rate, wandering a texel left or right every five rows and leaving
// a broken trail. A drop is a texel catching the light over a dark one,
// as water lenses the view: 1 the lit texel, -1 the dark, 0.5 the trail.
// With stain a runner leaves a faint track all the way down where it went,
// 0.25, until the next runs (nukage rain).
float bead(vec2 cell,float t,uint seed,float wet,out vec2 at) {
    at=vec2(-1e4);
    if(hash(cell,seed^0x2a1d64f3u)>wet*0.22) return 0.0;
    float period=4.0+5.0*hash(cell,seed^0x6b43a9b5u),clock=t+hash(cell,seed^0x1e3779b1u)*period;
    float cycle=floor(clock/period);
    if(clock-cycle*period>period*0.85) return 0.0;
    at=cell*vec2(5,6)+floor(vec2(hash(cell+cycle*13.0,seed^0x4cf5ad43u)*5.0,1.0+hash(cell+cycle*17.0,seed^0x7a2b3c4du)*5.0));
    return 1.0;
}
float runner(float x,float y,float top,float t,uint seed,float wet,bool stain) {
    float column=floor(x/7.0);
    for(int k=-1;k<=1;++k) {
        float col=column+float(k);
        if(hash(vec2(col,7),seed)>wet*0.7) continue;
        float period=3.0+5.0*hash(vec2(col,8),seed),clock=t+hash(vec2(col,9),seed)*period;
        float cycle=floor(clock/period),age=clock-cycle*period;
        float speed=12.0+14.0*hash(vec2(col,cycle),seed^0x5d0f1a2bu);
        float head=top-1.0-floor(age*speed);
        if(head<-2.0&&!stain) continue;
        float shift=floor(hash(vec2(col,floor(y/5.0)+cycle*31.0),seed^0x3c1f9e77u)*3.0)-1.0;
        float base=col*7.0+1.0+floor(hash(vec2(col,cycle+50.0),seed)*5.0);
        if(x!=base+shift) continue;
        if(y==head) return 1.0;
        if(y==head-1.0) return -1.0;
        if(y>head&&y<head+10.0&&hash(vec2(x,y+cycle*7.0),seed^0x18a4c3e9u)<0.5*(1.0-(y-head)/10.0)) return 0.5;
        if(stain&&y>head&&hash(vec2(x,y+cycle*7.0),seed^0x2f1e9a3du)<0.6) return 0.25;
    }
    return 0.0;
}
float dropsOnGlass(vec2 texel,vec2 extent,bool flipped,uint seed,float wet,bool stain) {
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float t=floor(c.fog.w*35.0)/35.0;
    float run=runner(x,y,floor(extent.y),t,seed,wet,stain);
    if(run!=0.0) return run;
    vec2 at;
    if(bead(floor(vec2(x/5.0,y/6.0)),t,seed,wet,at)>0.0&&at==vec2(x,y)) return 1.0;
    if(bead(floor(vec2(x/5.0,(y+1.0)/6.0)),t,seed,wet,at)>0.0&&at==vec2(x,y+1.0)) return -1.0;
    return 0.0;
}
