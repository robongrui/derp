// Brick wear (FEATURE_UNEVEN_WALLS): the bricks, blocks and plates painted
// into a wall (tiles.glsl's cells, each repeat's own by its anchor and the
// wall section's seed, vStatic) age each their own way, so a wall reads as
// laid brick by brick and no two repeats match.
// - Tone: a brick's whole face a step or two lighter or darker along its
//   palette ramp, a few greyer or warmer, snapped back to the palette (the
//   seam each cell starts with keeps the mortar's own). Flat per brick: no
//   gradients, no blotches across bricks.
// - Shadows: in the sun a brick standing proud of its neighbour (the tile's
//   height, tilePose) shades the mortar line under it, and the joint beside
//   it on the side away from the sun; one texel, the brick's own outline.
// Part of world.frag, after tiles.glsl, which declares and defines what these read.
const float brickToneStep=0.06;   // One step lighter or darker, of the face's brightness.
// The texel's brick (its anchor, unwrapped) when it lies on a brick's face,
// not on the seam the cell starts with or in a row without joints.
bool brickFace(vec2 uv,out ivec2 anchor) {
    ivec2 p=ivec2(floor(uv));bvec2 free;
    if(!tileAnchor(p,textureSize(relief,0),anchor,free)||any(free)) return false;
    return p.x!=anchor.x&&p.y!=anchor.y;
}
vec3 brickTone(vec3 color,vec2 uv) {
    ivec2 anchor;
    if(!brickFace(uv,anchor)) return color;
    uint h=tileHash(anchor,vStatic*0x9e3779b9u+0x51633e2du);
    float shift=float(int(h%5u)-2)*brickToneStep;   // -2 to +2 steps, most bricks off by one
    uint kind=(h>>8u)&7u;
    if(shift==0.0&&kind>1u) return color;
    vec3 c=color*(1.0+shift);
    if(kind==0u) c=mix(c,vec3(dot(c,vec3(0.299,0.587,0.114))),0.35);   // weathered greyer
    else if(kind==1u) c*=vec3(1.08,1.0,0.9);                          // fired warmer
    return texelFetch(paletteLUT,ivec3(round(saturate(c)*31.0)),0).rgb;
}
// How much of the sun this texel keeps (1 or less): on a mortar line or joint
// a brick standing proud of the one across it casts its edge onto it. toSun
// in the world, along: the way u runs along the wall (wallAlong).
float brickShadow(vec3 toSun,vec2 along) {
    ivec2 p=ivec2(floor(vUV)),size=textureSize(relief,0),own,other;bvec2 free,otherFree;
    if(!tileAnchor(p,size,own,free)||any(free)) return 1.0;
    float here=tilePose(own,free,vStatic).z;
    // The mortar line a cell starts with, under the brick above it: lit from above.
    if(p.y==own.y&&toSun.z>0.15&&tileAnchor(p-ivec2(0,1),size,other,otherFree)&&!any(otherFree)&&other!=own&&
       tilePose(other,otherFree,vStatic).z>here+0.4) return 0.35;
    // The joint a cell starts with: the brick before it (lower u) shades it
    // with the sun on that side, the cell's own brick with the sun beyond it.
    float side=dot(toSun.xy,along);
    if(p.x==own.x&&abs(side)>0.2&&tileAnchor(p-ivec2(1,0),size,other,otherFree)&&!any(otherFree)&&other!=own) {
        float before=tilePose(other,otherFree,vStatic).z;
        if(side<0.0?before>here+0.4:here>before+0.4) return 0.35;
    }
    return 1.0;
}
