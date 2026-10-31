// The weather's whiteout (scene3d/whiteout.cpp: snow, sand, ash, dry ice by the
// wind; in the colour snow.cpp, sand.cpp, ash.cpp and fog.cpp give c.haze: c.climate.z
// how thick, c.climate.w how much of it is round the eye, 1 out in it,
// lingering a while after coming in and a little by open doors): how much
// of a point at world fades into the blowing snow (c.haze) with distance,
// halfway in a few hundred units. What lies out in it (outside) always
// does; the rest as much as is round the eye. Every shader that draws in
// the world mixes it in (world.frag, glass.frag), or what it draws stands
// out sharp. Expects the Camera block c.
// whiteoutOver: for a view running storm units through the blowing snow
// (world.frag finds how much of its way lies outside, stormDistance).
float whiteoutOver(float storm) {
    if(c.climate.z<=0.0||storm<=0.0) return 0.0;
    return c.climate.z*(1.0-exp(-storm/(700.0*(1.4-c.climate.z))));
}
float whiteout(vec3 world,bool outside) {
    return whiteoutOver(length(world-c.eye.xyz)*(outside?1.0:c.climate.w));
}
