// Snow on the glass (glass.frag): the wet kind that clumps. Flakes stick
// to the pane and grow into clumps of a texel to three; where the room
// behind is heated (cold: 0 heated .. 1 as cold as outside) they melt
// where they touch the warm glass and, once heavy, slide down, slower than
// rain and wandering a texel every eight rows, with a dark lens under them
// and a broken wet trail behind, into the snow piled on the sill. Where the
// cold has got into the room they stay where they stuck and frost creeps
// in from the frame (frost.glsl). All of it as much as the wind presses
// snow onto this pane, the pile a texel to five deep with it. x and y
// count texels from the pane's first end and its sill.
// A clump in the 6-texel columns round x, born over the pile (floorY) and
// gone into it: 2 its body, 1 its rim, -1 the lens under it, 0.4 the wet
// trail, 0 none.
float snowClump(float x,float y,float floorY,float top,float t,uint seed,float amount,float slide) {
    float column=floor(x/6.0);
    for(int k=-1;k<=1;++k) {
        float col=column+float(k);
        if(hash(vec2(col,61),seed)>amount*0.8) continue;
        float period=5.0+4.0*hash(vec2(col,62),seed),clock=t+hash(vec2(col,63),seed)*period;
        float cycle=floor(clock/period),age=(clock-cycle*period)/period;
        float born=floorY+3.0+floor(hash(vec2(col,cycle+64.0),seed)*max(top-floorY-6.0,1.0));
        float size=1.0+floor(min(age/0.55,1.0)*2.0);         // grows a texel, two, three
        float sliding=max(age-0.55,0.0)*period*(3.0+3.0*hash(vec2(col,cycle+65.0),seed))*slide;
        float head=born-floor(sliding);
        if(head<floorY) continue;                             // gone into the pile
        float shift=sliding>0.0?floor(hash(vec2(col,floor(y/8.0)+cycle*29.0),seed^0x3c1f9e77u)*3.0)-1.0:0.0;
        float left=col*6.0+1.0+floor(hash(vec2(col,cycle+66.0),seed)*3.0)+shift;
        float tall=size>1.0?2.0:1.0;
        bool across=x>=left&&x<left+size;
        if(!across) continue;
        if(y>=head&&y<head+tall) return size>2.0&&y==head+tall-1.0&&(x==left||x==left+size-1.0)?1.0:2.0;
        if(sliding>0.0&&y==head-1.0) return -1.0;
        if(sliding>0.0&&y>=head+tall&&y<born+tall&&hash(vec2(x,y+cycle*7.0),seed^0x18a4c3e9u)<0.45) return 0.4;
    }
    return 0.0;
}
void snowGlass(inout vec4 result,vec2 texel,vec2 extent,bool flipped,uint seed,float wet,float press,float cold,vec3 lit,float light,float grazing,float edge) {
    float amount=wet*(0.2+0.8*press);
    float x=floor(flipped?extent.x-texel.x:texel.x),y=floor(texel.y);
    float t=floor(c.fog.w*35.0)/35.0;
    vec3 white=saturate(vec3(0.86,0.9,0.95)*min(vec3(0.15)+lit*0.85,vec3(1.2)));
    float pile=1.0+floor(hash(vec2(floor(x/3.0),41),seed)*(1.0+4.0*press)*amount);
    if(y<pile) {over(result,white,0.9);return;}
    float clump=snowClump(x,y,pile,floor(extent.y),t,seed,amount,1.0-smoothstep(0.3,0.7,cold));
    if(clump>1.5) {over(result,white,0.85);return;}
    if(clump>0.5) {over(result,white,0.55);return;}
    if(clump<0.0) {over(result,vec3(0.04,0.05,0.06)*light,0.3);return;}
    if(clump>0.0) {over(result,saturate(vec3(0.82,0.92,1.0)*min(vec3(0.2)+lit*0.8,vec3(1.2))),0.2);return;}
    float frost=frostCreep(x,y,extent,seed,amount*smoothstep(0.4,0.9,cold));
    if(frost>0.0) over(result,saturate(vec3(0.86,0.92,0.97)*min(vec3(0.2)+lit*0.8,vec3(1.2))),frost>0.75?0.55:0.3);
}
