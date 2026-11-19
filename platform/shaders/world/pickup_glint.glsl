// Pickups (MODE_PICKUP): now and then a glint runs across the brightest
// texels of the artwork (a medkit's cross, a clip's brass, a visor) as if
// the item caught a light. Nothing else about a pickup glows: finding it in
// the dark comes from the eye catching movement, not from a halo.
// Part of world.frag, which declares the resources these read; tileHash
// comes from world/tiles.glsl.
// One narrow diagonal band, top left to bottom right, in two hard steps,
// texel by texel on the 35 Hz tic clock. Each item keeps its own rhythm
// (seed: 0-255 from the item, geometry.cpp), and some passes are skipped
// so a room full of ammo never blinks in step.
// Returns how far a texel is lifted toward its full palette colour: 1 in the
// band's core, 0.5 at its edges, 0 elsewhere.
float pickupGlint(vec3 paint,vec2 uv,uint seed) {
    float hi=max(paint.r,max(paint.g,paint.b));
    float catches=hi>0.45?1.0:hi>0.3?0.5:0.0;   // Dark paint never catches it.
    if(catches==0.0) return 0.0;
    float period=3.0+float(seed&7u)*0.35;        // 3 - 5.5 s between passes.
    float now=floor(c.fog.w*35.0)/35.0+float(seed)*0.071;
    float cycle=floor(now/period);
    uint pass=tileHash(ivec2(int(cycle),int(seed)),0x6c8e9cf5u);
    if((pass&3u)==0u) return 0.0;                 // A quarter of the passes skipped.
    float sweep=(now-cycle*period)/0.4;           // 0.4 s across the item.
    if(sweep>1.0) return 0.0;
    ivec2 size=textureSize(image,0),t=ivec2(floor(uv));
    // The band leans a little differently each pass.
    float lean=1.0+float((pass>>2)&3u)*0.25;
    float front=mix(-3.0,float(size.x)*lean+float(size.y)+3.0,sweep);
    float d=abs(float(t.x)*lean+float(t.y)-front);
    float band=d<1.0?1.0:d<2.5?0.5:0.0;
    return band*catches;
}
