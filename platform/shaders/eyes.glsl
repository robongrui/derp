// The player's eyes (scene3d eyes.cpp; present.frag): c.eyes.x how far the lids have closed (0-1),
// down from the world view's top and up from its bottom, a little more at the sides as lids do,
// their edge in whole Doom pixel rows; c.eyes.y how dark it is between them (asleep, out cold,
// dead: 1 is black). The view's place on the screen: z its top, w its height (uv). The 2D layer
// (status bar, menus) stays.
vec3 closedEyes(vec3 color) {
    if(overlayCovers(vUV)) return color;
    float y=floor((vUV.y-c.eyes.z)/max(c.eyes.w,1e-4)*200.0)/200.0;   // the view's Doom pixel rows, 0-1
    float side=abs(vUV.x-0.5)*2.0;
    float lid=c.eyes.x*(0.5+0.06*side*side);
    if(c.eyes.x>0.0&&(y<lid||1.0-y<lid)) return vec3(0.0);
    return color*(1.0-c.eyes.y);
}
