// Dead trees in a sky of fog (scene3d/sky_trees.cpp; the 2025 Heretic's fifth):
// the painting's foot stays a flat picture on the horizon, the trees in the
// fog they were painted in, with the engine's clouds above it and the valley
// mist drifting across their feet as among Doom's first mountains. The trees
// are cut out so the fog behind them is the painting's own, filled (no ghost of
// them in the clouds), and laid back exactly where they were painted, in its
// own texels. (Raised on rings round the map they slid against each other as
// cards and read wrong.) Part of sky.frag, after the clouds.
vec4 treeTexel() {return texelFetch(layers,ivec2(7,paintSize().y+1),0);}
#ifdef SKY_TREE
bool treeSky() {return SKY_TREE!=0;}   // a sky variant built for one kind (gpu3d's skyTwin)
#else
bool treeSky() {return treeTexel().r>0.5/255.0;}
#endif
vec3 treeHorizon(vec3 color,vec3 ray) {
    ivec2 size=depthSize();
    float rise=ray.z/max(length(ray.xy),1e-4),row=(skyHorizon()-rise*skyScale())*float(size.y),top=treeTexel().g*255.0;
    // The strip: the clouds above run into the painted fog over a long reach,
    // from the tallest tree's tip to well down among them. Eased in over a
    // few rows, its top (a line at one height round the sky) stood over the
    // picture as a dark dome wherever the clouds were darker than the fog.
    float strip=smoothstep(top-6.0,top+45.0,row);
    if(strip<=0.0)return color;
    float u=aroundU(ray.xy,size);
    vec2 uv=vec2(u,clamp(row,0.5,float(size.y)-0.5));
    float shade=mix(1.0,weatherShade(),0.8);   // as sky.frag dims the rest of the sky
    // The fog takes the clouds' own tone as weather closes in: under rain
    // the deck goes dark and the pale painted fog stood out under it.
    vec3 fog=layerBlend(uv,0,0u,size)*shade;
    fog=mix(fog,color,0.6*max(overcastDeck(),saturate(c.weather.x*2.0)));
    color=mix(color,fog,strip);
    // The trees whole, wherever the strip has faded in or not.
    vec4 tree=texelFetch(layers,ivec2(wrapColumn(int(floor(u)),size.x),int(uv.y)),0);
    color=mix(color,mix(layerColor(tree.b),layerColor(treeTexel().b),0.3*thickAir())*shade,tree.a);
    // The valley mist across their feet, on its ring, drifting with the wind.
    vec3 o=c.eye.xyz-c.skyLayers.xyz;
    float m=circleExit(o.xy,ray.xy,c.skyShape.y);
    return m>0.0?ringMist(color,o+ray*m,size):color;
}
