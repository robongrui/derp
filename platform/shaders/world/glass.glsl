// Monitor glass in computer textures: where a texel sits on its screen and
// the curved CRT dome over it.
// Part of world.frag, which declares the resources these read.
bool isGlass(vec2 uv) {
    return texelFetch(image,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0).a<0.999;
}
// Position across the screen in [-1,1], and the screen's top-left texel and
// size in texels.
vec2 glassScreen(vec2 uv,out vec2 origin,out vec2 extent) {
    vec4 f=texelFetch(glassFrame,wrapTexel(ivec2(floor(uv)),textureSize(image,0)),0)*255.0;
    origin=floor(uv)-round(f.rg);extent=max(round(f.ba),vec2(1));
    return clamp((uv-origin)/extent*2.0-1.0,vec2(-1),vec2(1));
}
// A shot-out screen (MODE_DEAD_GLOW): cracks run out from where the shot
// struck, set per screen and texture repeat, and on the texel grid, plus a
// ring of fracture through some of the wedges. Returns 1 on a crack texel;
// shard numbers the wedge between two cracks the texel lies in.
float glassCracks(vec2 uv,vec2 origin,vec2 extent,uint seed,out uint shard) {
    const int rays=6;
    vec2 texel=floor(uv)+0.5;
    ivec2 repeat=ivec2(floor(uv/vec2(textureSize(image,0))));
    uint h=tileHash(repeat*1024+ivec2(origin),seed);
    vec2 hit=origin+extent*(0.3+0.4*vec2(float(h&255u),float((h>>8)&255u))/255.0);
    vec2 d=texel-hit;
    float r=length(d),angle=atan(d.y,d.x)+3.14159265,crack=r<1.0?1.0:0.0;
    shard=0u;
    for(int i=0;i<rays;++i) {
        float a=(float(i)+0.8*float((h>>(16+2*i))&3u)/3.0)*6.2831853/float(rays);
        if(angle>a) shard=uint(i+1);
        vec2 along=vec2(cos(a-3.14159265),sin(a-3.14159265));
        if(dot(d,along)>0.0&&abs(d.x*along.y-d.y*along.x)<0.5) crack=1.0;
    }
    if(shard==uint(rays)) shard=0u; // The last wedge wraps into the first.
    float ring=min(extent.x,extent.y)*(0.3+0.2*float((h>>28)&3u)/3.0);
    if(abs(r-ring)<0.5&&(tileHash(ivec2(int(shard),0),h)&1u)!=0u) crack=1.0;
    return crack;
}
// FEATURE_LIVE_READOUTS: blocks of LED cells outside the screens
// (doom_screen_readouts; the frame holds where a texel sits on its block,
// zero elsewhere). Old console lamps on a plain flipbook loop: each cell is
// a whole lamp, only on or off, and a block steps through eight frames,
// 0.7 seconds each, over and over: one loop goes as painted twice, all out,
// a sparse frame, all on twice, a dense frame, a sparse one; the other mixes
// the same frames in another order. A frame's pattern is fixed per block, so
// the loop repeats exactly. Each block picks its loop and where it starts,
// so neighbouring blocks (often painted alike) show different frames on the
// same beat. A lamp switched away from its
// painting shows a lamp painted that way, from its own row when one is,
// so the block keeps the artwork's colors.
// Returns the texture coordinate to read in place of uv.
bool readoutLit(ivec2 p,ivec2 size) {
    return (int(round(texelFetch(glassFrame,wrapTexel(p,size),0).r*255.0))&128)!=0;
}
vec2 readoutCell(vec2 uv,uint section) {
    const int fill[16]=int[16](-1,-1,0,30,100,100,70,30, // Percent of lamps on; -1 as painted.
                               -1,30,-1,0,70,100,-1,30);
    ivec2 p=ivec2(floor(uv)),size=textureSize(glassFrame,0);
    vec4 f=texelFetch(glassFrame,wrapTexel(p,size),0)*255.0;
    if(f.b<0.5) return uv;
    ivec4 bytes=ivec4(round(f));
    ivec2 cell=ivec2(1)+ivec2(notEqual(bytes.ba&128,ivec2(0))),extent=bytes.ba&127,local=bytes.rg&127;
    ivec2 origin=p-local,at=local/cell,grid=max((extent+cell-1)/cell,ivec2(1));
    int lamp=at.y*grid.x+at.x,lamps=grid.x*grid.y;
    bool painted=(bytes.r&128)!=0;
    uint wall=tileHash(ivec2(int(section&0xffffu),int(section>>16)),0x5bd1e995u),seed=tileHash(origin,wall);
    int frame=((int(c.fog.w/0.7)+int(seed&7u))&7)|int((seed>>3)&8u);
    bool on=fill[frame]<0?painted:int(tileHash(at,seed^uint(frame)*0x9e3779b9u)%100u)<fill[frame];
    if(on==painted) return uv;
    ivec2 within=local-at*cell;
    for(int k=0;k<10;++k) {
        uint h=tileHash(ivec2(lamp,k),seed);
        ivec2 from=k<6?ivec2(int(h%uint(grid.x)),at.y):ivec2(int(h%uint(grid.x)),int((h>>8)%uint(grid.y)));
        ivec2 q=origin+min(from*cell+within,extent-1);
        if(readoutLit(q,size)==on) return vec2(q)+fract(uv);
    }
    return uv;
}
// The CRT dome, peaking at a quarter of the shorter side: its smooth normal.
vec3 glassDome(vec2 screen,vec2 extent,vec3 normal,vec3 tangent,vec3 bitangent) {
    float peak=min(extent.x,extent.y)*0.25;
    vec2 slope=-peak*4.0*screen*(1.0-screen.yx*screen.yx)/extent;
    return normalize(normal-(slope.x*tangent+slope.y*bitangent));
}
