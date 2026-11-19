/* What kind of sky the layers paint (sky_layers.cpp), read off the same settings row sky.frag
   reads (sky/clouds.glsl's cloudSkyTexel, sky/hell.glsl, sky/spire.glsl, sky/trees.glsl), so the
   GPU side can draw it with a sky shader built for that kind alone (gpu3d/draw.cpp's skyTwin). */
#pragma once

namespace doom3d {
struct SkyKind {
    bool valid=false;        // layers on and painted: the rest means something
    bool cloud=false;        // cloudSky(): texel 1 alpha
    bool city=false;         // citySky(): texel 3 red
    int hell=0;              // hellKind(): texel 5 red (1, 3: hellSky(); 2: red clouds)
    bool spire=false;        // spireSky(): texel 6 red
    bool tree=false;         // treeSky(): texel 7 red
    bool field=false;        // fieldSky(): texel 12 red
};
// This level's sky as last painted (skyLayersImage paints it when needed).
SkyKind skyKind();
}
