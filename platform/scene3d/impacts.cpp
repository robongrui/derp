/* Bullet impacts and explosions: wall and floor decals, scorches and
   debris. */
#include "internal.h"
#include "blood_material.h"

namespace doom3d {
// Hitscan wall impacts leave fading bullet marks and a short burst of debris
// tinted by the wall texture's average color. Render-only: no game state,
// own random numbers, cleared on level changes and loads.
struct WallDecal { int line,side,section; float along,dz,born,size; bool flip;int mark=0; };   // mark: its own decal key (wall_hits.cpp)
struct Scorch { float x,y,born,size;int sector;int mark=0; };   // mark: a blast's own ragged shape (blastScorch), else the fireball frame's
static std::vector<WallDecal> wallDecals;
std::vector<FlatDecal> flatDecals;
std::vector<Debris> debris;
static std::vector<Scorch> scorches;
static unsigned decalLevelSerial;
std::minstd_rand impactRandom(1993);
// Impacts from a previous level go once the engine has moved on.
void dropOldLevelImpacts() {
    if(decalLevelSerial!=r_levelserial){wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();decalLevelSerial=r_levelserial;}
}
// A block size map units across (tall times as high), but never under one
// Doom pixel, set on the Doom-pixel grid at its depth: a pixel of an old
// sprite rather than a soft speck. The color is lit, then put in the palette
// (unless palette is false: liquid crests match the smoothly shaded water).
// With taken, a block whose Doom-pixel corner is already in it is skipped.
// A resting block stands on the point instead of centering on it, so a floor
// does not swallow half of it. Given its unlit color, the flashlight beam
// lights it too. Blocks wholly off screen are dropped before any of that.
BlockCells crestCells;
float distantShare(const Uniforms &camera,float x,float y,float z,float size) {
    float depth=(x-camera.eye[0])*camera.forward[0]+(y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
    if(depth<4)return 1;
    float covers=size*100*camera.projection[1]/depth;   // Doom pixels across
    return covers>=1?1:std::max(0.1f,covers*covers);
}
void pixelBlock(std::vector<Vertex> &out,const Uniforms &camera,float x,float y,float z,float size,float tall,const std::array<float,3> &color,bool resting,
                const std::array<float,3> *albedo,bool palette,BlockCells *taken,float alpha,float stipple,float least,unsigned modeBits) {
    float d[3]={x-camera.eye[0],y-camera.eye[1],z-camera.eye[2]};
    float depth=d[0]*camera.forward[0]+d[1]*camera.forward[1]+d[2]*camera.forward[2];
    if(depth<4)return;
    float unit=depth/(100*camera.projection[1]),across=100*camera.projection[1]/camera.projection[0]; // Map units per Doom pixel; half the view in Doom pixels.
    float px=(d[0]*camera.right[0]+d[1]*camera.right[1]+d[2]*camera.right[2])/unit;
    float py=(d[0]*camera.up[0]+d[1]*camera.up[1]+d[2]*camera.up[2])/unit;
    float w=std::max(1.0f,std::round(size/unit)),h=std::max(1.0f,std::round(size*tall/unit));
    if(size/unit<1)w=std::max(least,size/unit);
    if(size*tall/unit<1)h=std::max(least,size*tall/unit);
    float left=std::round(px+across-w*0.5f)-across,bottom=std::round(py+100-(resting?0:h*0.5f))-100;
    if(w<1)left=px-w*0.5f;   // smaller than the grid: it can't sit on it
    if(h<1)bottom=py-(resting?0:h*0.5f);
    if(left+w<-across-2||left>across+2||bottom+h<-102||bottom>102)return;
    if(taken&&!taken->take((int)std::lround(left),(int)std::lround(bottom),across))return;
    std::array<float,3> shade=color;
    if(albedo&&flashlightOn&&flashes.count&&flashes.lights[0].direction[3]>0&&camera.effects[2]==0) {
        const Flash &beam=flashes.lights[0];
        float amount=flashAt(x,y,z,beam);
        // Capped at white. Unsnapped lit specks (chips, dust: particleColour) only at what they were already, so
        // one past white keeps its hue; everything snapped (rain, snow, sand, ash, blood) as it always was.
        if(amount>0)for(int c=0;c<3;++c)shade[c]=std::min(palette?1.0f:std::max(1.0f,shade[c]),shade[c]+(*albedo)[c]*amount*beam.color[c]);
    }
    auto lit=palette?paletteColor(shade):shade;
    // Out under the sky: the outdoor haze takes it far off (flat.frag), as it does the walls behind it. Not
    // a liquid's crests (they brighten what's under them).
    unsigned mode=modeBits;
    if(&out!=&crestVertices) {
        const sector_t *at=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        if(at->ceilingpic==skyflatnum)mode|=MODE_PARTICLE_OUTSIDE;
    }
    auto vertex=[&](float sx,float sz) {
        float r=(sx-px)*unit,u=(sz-py)*unit;
        Vertex v={x+camera.right[0]*r+camera.up[0]*u,y+camera.right[1]*r+camera.up[1]*u,z+camera.right[2]*r+camera.up[2]*u,0,0,alpha,mode};
        v.red=lit[0];v.green=lit[1];v.blue=lit[2];return v;
    };
    if(stipple>=1) {quad(out,vertex(left,bottom+h),vertex(left+w,bottom+h),vertex(left+w,bottom),vertex(left,bottom));return;}
    // Stippled: each Doom pixel of the block drawn where a 4 x 4 ordered pattern, laid from the block's own
    // corner, is under the share; a single pixel stays (dropping it would be the caller's choice).
    static const int bayer[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    int wi=(int)w,hi=(int)h;
    if(wi*hi<=1) {quad(out,vertex(left,bottom+h),vertex(left+w,bottom+h),vertex(left+w,bottom),vertex(left,bottom));return;}
    for(int j=0;j<hi;++j)for(int i=0;i<wi;++i) {
        if((bayer[j&3][i&3]+0.5f)/16>stipple)continue;
        float l=left+i,b=bottom+j;
        quad(out,vertex(l,b+1),vertex(l+1,b+1),vertex(l+1,b),vertex(l,b));
    }
}
// A small thing in the air's colour in the light round it (blood thrown, chips off a wall): the baked
// lamps' (by a red lamp redder), the flashlight's beam. wet (blood): blood's material (blood_material.h, as the
// shader lights its decals): no floor in the dark, where it is black liquid, the light keeping part of its hue,
// its brightest channel rolled off, and it gives back some of the light in that light's colour. Dry (dust, smoke,
// chips): lit as a wall is, the flashlight's beam left to pixelBlock (it moves, what's lit here stays). Dry
// colours can pass 1 and are drawn without the palette snap (see the callers), as the walls are.
std::array<float,3> particleColour(const std::array<float,3> &albedo,float x,float y,float z,bool wet) {
    // The light's colour times the blood's (the rain's light, taken on white): additive, its grey lift turned
    // the dark red brown.
    // The light round it as a sprite's there (bakedLightAt: the lamps and the bounce, past 1 in a bright room),
    // so a drop is as bright as the blood it lands as: the rain's light (weatherColor) stops at 1, and a drop
    // there showed its bare palette red, near black against a room lit far brighter (the user, MAP01).
    const sector_t &sector=*R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    auto light=gridLightActive()?bakedLightAt(x,y,z,&sector):weatherColor(sector,x,y,z,{1,1,1},wet);
    // Dry (dust, chips, smoke): the wall's own rule (world.frag), so a chip is lit as the wall it came off. Its
    // colour times every channel of the light there (a grey speck in green spill goes green; the user), plus the
    // light spilling into the channels a coloured speck lacks, by how coloured it is. No floor, no lift, no
    // clamp: the old ones made specks either black or white, by a switch at the room's fill level (the user).
    if(!wet) {
        float most=std::max({albedo[0],albedo[1],albedo[2],1e-3f}),chroma=most-(0.299f*albedo[0]+0.587f*albedo[1]+0.114f*albedo[2]);
        std::array<float,3> lit;
        for(int c=0;c<3;++c)lit[c]=albedo[c]*light[c]+light[c]*(1-albedo[c]/most)*chroma*0.6f;
        return lit;
    }
    float room=lighting(&sector),floor=room,peak=std::max({light[0],light[1],light[2],1e-3f}),level=std::clamp(std::max(floor,peak*1.13f),floor,2.2f);
    // The flashlight's amber is left out of the hue (it read as amber sparkles): in the beam the drop goes a
    // lighter red instead (sheen), lamps' and glowing floors' colours stay.
    float beam=std::min(1.0f,beamAt(x,y,z)*1.5f);
    std::array<float,3> lit;
    // The light's own hue only partly: by a strongly coloured lamp (Doom 2 MAP01's corridor under GRNLITE1, its
    // light green) red blood times green light left nothing. A channel the light lacks keeps at least 60%
    // (blood: bloodLight, the decals' rule).
    auto adapted=bloodLight(light);
    level=bloodLightCap(level);
    for(int c=0;c<3;++c) {
        float hue=adapted[c]/peak;
        lit[c]=albedo[c]*level*(hue+(1-hue)*beam);
    }
    // And the light itself on top: wet and a little see-through, a drop gives back some of the light that
    // strikes it in that light's own colour, the more the brighter it is there (none in the dark). Coloured by
    // multiplying alone, a bright green corridor turned red blood black; it goes red with a green sheen (the user).
    float sheen=0.16f*std::clamp((peak-0.4f)/0.6f,0.0f,1.0f);
    for(int c=0;c<3;++c)lit[c]=std::min(1.0f,lit[c]+sheen*light[c]/peak);
    return lit;
}
// Section 0 is a one-sided wall (anchored to its floor), 1 an upper wall
// (anchored to the back ceiling, e.g. a door face), 2 a lower wall.
bool wallSpan(const line_t &line,int side,int section,WallSpan &span) {
    if(line.sidenum[side]<0)return false;
    const side_t &s=sides[line.sidenum[side]];
    const sector_t *front=s.sector,*back=side?line.frontsector:line.backsector;
    span.a={units((side?line.v2:line.v1)->x),units((side?line.v2:line.v1)->y)};
    span.b={units((side?line.v1:line.v2)->x),units((side?line.v1:line.v2)->y)};
    if(section==0) {if(back)return false;span.bottom=floorZ(front);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.midtexture;}
    else if(!back) return false;
    else if(section==1) {span.bottom=ceilZ(back);span.top=ceilZ(front);span.anchor=span.bottom;span.texture=s.toptexture;}
    else {span.bottom=floorZ(front);span.top=floorZ(back);span.anchor=span.top;span.texture=s.bottomtexture;}
    return span.top>span.bottom;
}
void wallImpact(line_t *line,int side,fixed_t fx,fixed_t fy,fixed_t fz,mobj_t *shooter,fixed_t dx,fixed_t dy,fixed_t slope) {
    if(!line||gamestate!=GS_LEVEL)return;
    dropOldLevelImpacts();
    float x=units(fx),y=units(fy),z=units(fz);
    const sector_t *back=side?line->frontsector:line->backsector;
    int section=!back?0:z>=ceilZ(back)?1:z<=floorZ(back)?2:-1;
    WallSpan span;if(section<0||!wallSpan(*line,side,section,span))return;
    float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);if(len<1)return;
    float along=std::clamp(((x-span.a.x)*ex+(y-span.a.y)*ey)/len,0.0f,len);
    float now=gameSeconds();
    if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
    wallDecals.push_back({int(line-lines),side,section,along,z-span.anchor,now,randomRange(5.0f,7.5f),randomRange(0,1)<0.5f});
    if(span.texture<=0)return;
    int texture=texturetranslation[span.texture];
    Image &image=wallImage(texture);
    float light=lighting(sides[line->sidenum[side]].sector);
    float nx=ey/len,ny=-ex/len,tx=ex/len,ty=ey/len;
    float wx=span.a.x+tx*along+nx*1.5f,wy=span.a.y+ty*along+ny*1.5f;
    if(settings.wallHits) {
        wallDecals.back().mark=wallHit(texture,image,wx,wy,z,nx,ny,shooter,units(dx),units(dy),units(slope));
        shotWall(*line,side,section,along,x,y,z);
        return;
    }
    if(debris.size()>512)debris.erase(debris.begin(),debris.begin()+(debris.size()-512));
    for(int i=0;i<6;++i) {
        float out=randomRange(50,140),slide=randomRange(-55,55);
        debris.push_back({wx,wy,z,nx*out+tx*slide,ny*out+ty*slide,randomRange(20,120),now,randomRange(0.45f,0.8f),randomRange(1.2f,2.0f),texelColor(image),light,true});
    }
    shotWall(*line,side,section,along,x,y,z);
}
// A bullet off armour (the revenant's chest plate): a spray of warm sparks back out along (nx, ny), up a
// little and falling, and a flash; the more damage the more sparks.
void metalSparks(float x,float y,float z,float nx,float ny,int damage) {
    if(gamestate!=GS_LEVEL)return;
    dropOldLevelImpacts();
    float now=gameSeconds();
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    for(int i=0,count=4+std::min(damage,30)/5;i<count;++i) {
        float out=randomRange(80,240),across=randomRange(-120,120),heat=randomRange(0,1);
        debris.push_back({x+nx*2,y+ny*2,z,nx*out-ny*across,ny*out+nx*across,randomRange(-30,150),now,randomRange(0.1f,0.3f),
                          randomRange(0.7f,1.1f),{1.0f,0.75f+0.25f*heat,0.35f+0.5f*heat*heat}});
    }
    sparkFlash(x+nx*2,y+ny*2,z,48,0.5f,{1.0f,0.85f,0.6f});
}
void barrelBlast(const mobj_t &thing,float x,float y,float z) {
    dropOldLevelImpacts();
    if(!explosionFront(x,y,z+24,128,true,true)) {glassBlast(x,y,z+24,128);blastLights(x,y,z+24,128);}
    if(explosionsOn())return;   // the blast's own scorch and sparks (explosions.cpp)
    float now=gameSeconds();
    if(scorches.size()>=48)scorches.erase(scorches.begin());
    scorches.push_back({x,y,now,randomRange(60,76),int(thing.subsector->sector-sectors)});
    if(debris.size()>480)debris.erase(debris.begin(),debris.begin()+(debris.size()-480));
    for(int i=0;i<32;++i) {
        float angle=randomRange(0,2*doomPi),speed=randomRange(60,220),heat=randomRange(0,1);
        debris.push_back({x,y,z+randomRange(8,32),std::cos(angle)*speed,std::sin(angle)*speed,randomRange(80,260),now,
            randomRange(0.6f,1.3f),randomRange(1.0f,2.2f),{1.0f,0.45f+0.5f*heat,0.08f+0.3f*heat*heat}});
    }
}
// A blast's scorch: a burnt middle with tongues out from it, never round, in six cuts.
static int scorchMark() {
    constexpr int size=48,base=-0x77000000;
    int variant=(int)randomRange(0,5.99f),key=base-variant;
    if(!images.count(key)) {
        std::vector<byte> pixels((size_t)size*size*2,0);
        uint32_t seed=hash32((uint32_t)(variant+1)*0x9e3779b9u);
        for(int j=0;j<size;++j)for(int i=0;i<size;++i) {
            float x=i+0.5f-size/2.0f,y=j+0.5f-size/2.0f,d=std::hypot(x,y),turn=(std::atan2(y,x)+doomPi)/(2*doomPi);
            // Round the turn both ways, blended across it: no seam where it closes.
            float one=valueNoise(turn*22,3.5f,1,seed),other=valueNoise((1-turn)*22,9.5f,1,seed);
            float tongue=one*(1-turn)+other*turn;
            float reach=(size/2.0f-1)*(0.42f+0.58f*tongue*tongue)+2.5f*(valueNoise(i+64.0f,j+64.0f,3,seed)-0.5f);
            float cover=std::clamp((reach-d)/4,0.0f,1.0f)*(0.75f+0.25f*valueNoise(i+16.0f,j+16.0f,2,seed^7u));
            pixels[((size_t)j*size+i)*2+1]=(byte)std::lround(cover*255);
        }
        images.emplace(key,upload(size,size,pixels.data()));
    }
    return -1-key;
}
void blastScorch(float x,float y,float z,float size) {
    dropOldLevelImpacts();
    float now=gameSeconds();
    const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
    if(z-floorZ(sector)<56) {
        if(scorches.size()>=48)scorches.erase(scorches.begin());
        scorches.push_back({x,y,now,size*randomRange(0.9f,1.15f),int(sector-sectors),scorchMark()});
    }
    // And on the nearest wall, if it went off beside one.
    WallHit nearest;nearest.t=1e9f;float way[2]={};
    for(int k=0;k<8;++k) {
        float a=k*doomPi/4,dx=std::cos(a),dy=std::sin(a);WallHit hit;
        if(firstSolidWall(x,y,z,dx,dy,0,56,hit)&&hit.t<nearest.t) {nearest=hit;way[0]=dx;way[1]=dy;}
    }
    WallSpan span;
    if(nearest.t<1e8f&&wallSpan(lines[nearest.line],nearest.side,nearest.section,span)) {
        float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
        if(len>=1) {
            float hx=x+way[0]*nearest.t,hy=y+way[1]*nearest.t;
            float along=std::clamp(((hx-span.a.x)*ex+(hy-span.a.y)*ey)/len,0.0f,len);
            if(wallDecals.size()>=192)wallDecals.erase(wallDecals.begin());
            wallDecals.push_back({nearest.line,nearest.side,nearest.section,along,z-span.anchor,now,size*(1-nearest.t/80),randomRange(0,1)<0.5f,scorchMark()});
        }
    }
}
// A square decal on a sector's floor or ceiling, clipped to its leaves.
// Out under the sky a mark sinks into the weather's whiteout like the
// surface it lies on (MODE_DECAL_OUTSIDE, shadow.frag).
static unsigned decalMode(const sector_t &sector) {return sector.ceilingpic==skyflatnum?MODE_DECAL_OUTSIDE:0u;}
static void flatDecal(int sectorIndex,float x,float y,float h,float z,int lump,const Image &image,float fade,bool flip) {
    unsigned mode=decalMode(sectors[sectorIndex]);
    std::vector<Point> square={{x-h,y-h},{x+h,y-h},{x+h,y+h},{x-h,y+h}};
    for(int leaf:sectorFloors[sectorIndex]) {
        const auto &b=floorBounds[leaf];
        if(b[2]<x-h||b[0]>x+h||b[3]<y-h||b[1]>y+h)continue;
        auto polygon=floors[leaf];
        for(size_t n=0;n<square.size()&&!polygon.empty();++n) {
            Point a=square[n],c=square[(n+1)%square.size()];
            polygon=clip(polygon,a,{c.x-a.x,c.y-a.y},false);
        }
        auto vertex=[&](Point p){float u=(p.x-x+h)/(2*h);return Vertex{p.x,p.y,z,(flip?1-u:u)*image.width,(y+h-p.y)/(2*h)*image.height,fade,mode};};
        for(size_t n=1;n+1<polygon.size();++n)
            for(Point p:{polygon[0],polygon[n],polygon[n+1]})decalBatches[lump].push_back(vertex(p));
    }
}
// The game's sprite for an effect role; no frames when the game has none.
static const spritedef_t &effectSprite(game_sprite_role role) {
    static const spritedef_t none={0,nullptr};
    int sprite=Game_Sprite(role);
    return sprite>=0&&sprite<numsprites?sprites[sprite]:none;
}
void buildImpacts(const Uniforms &camera) {
    for(auto &batch:decalBatches)batch.second.clear();
    particleVertices.clear();glowVertices.clear();crestVertices.clear();crestCells.clear();
    dropOldLevelImpacts();
    float now=gameSeconds();
    scorches.erase(std::remove_if(scorches.begin(),scorches.end(),[&](const Scorch &d){return now-d.born>60||now<d.born;}),scorches.end());
    const auto &blast=effectSprite(GAME_SPRITE_EXPLOSION);
    if(!scorches.empty()) {
        // Scorches reuse a fireball frame's coverage (or have a blast's own shape), clipped to the sector floor.
        int lump=blast.numframes>2?firstspritelump+blast.spriteframes[2].lump[0]:-1;
        for(const auto &d:scorches) {
            const sector_t &sector=sectors[d.sector];if(sector.floorpic==skyflatnum)continue;
            if(!d.mark&&lump<0)continue;
            float fade=std::clamp((60-(now-d.born))/8,0.0f,1.0f)*(d.mark?0.8f:0.7f);
            flatDecal(d.sector,d.x,d.y,d.size*0.5f,floorZ(&sector)+0.08f,d.mark?d.mark:lump,d.mark?images.at(-1-d.mark):lumpImage(lump,false),fade,false);
        }
    }
    wallDecals.erase(std::remove_if(wallDecals.begin(),wallDecals.end(),[&](const WallDecal &d){return now-d.born>40||now<d.born;}),wallDecals.end());
    flatDecals.erase(std::remove_if(flatDecals.begin(),flatDecals.end(),[&](const FlatDecal &d){return now-d.born>40||now<d.born;}),flatDecals.end());
    const auto &puff=effectSprite(GAME_SPRITE_PUFF);
    if(puff.numframes>2) {
        int lump=firstspritelump+puff.spriteframes[2].lump[0];Image &image=lumpImage(lump,false);
        for(const auto &d:flatDecals) {
            const sector_t &sector=sectors[d.sector];
            if((d.ceiling?sector.ceilingpic:sector.floorpic)==skyflatnum)continue;
            if(d.ceiling?camera.eye[2]>ceilZ(&sector):camera.eye[2]<floorZ(&sector))continue; // Back of the flat.
            float fade=std::clamp((40-(now-d.born))/4,0.0f,1.0f)*0.6f;
            flatDecal(d.sector,d.x,d.y,d.size*0.5f,d.ceiling?ceilZ(&sector)-0.08f:floorZ(&sector)+0.08f,lump,image,fade,d.flip);
        }
        for(const auto &d:wallDecals) {
            WallSpan span;if(!wallSpan(lines[d.line],d.side,d.section,span))continue;
            float ex=span.b.x-span.a.x,ey=span.b.y-span.a.y,len=std::hypot(ex,ey);
            if(ex*(camera.eye[1]-span.a.y)-ey*(camera.eye[0]-span.a.x)>0)continue; // Back of the wall.
            float zc=span.anchor+d.dz,h=d.size*0.5f;
            float u0=std::max(0.0f,d.along-h),u1=std::min(len,d.along+h),z0=std::max(span.bottom,zc-h),z1=std::min(span.top,zc+h);
            if(u1<=u0||z1<=z0)continue;
            float fade=std::clamp((40-(now-d.born))/4,0.0f,1.0f)*0.6f;
            float tx=ex/len,ty=ey/len,nx=ty*0.5f,ny=-tx*0.5f;
            const Image &mark=d.mark?images.at(-1-d.mark):image;
            auto texU=[&](float u){float t=(u-(d.along-h))/(2*h);return (d.flip?1-t:t)*mark.width;};
            auto texV=[&](float z){return ((zc+h)-z)/(2*h)*mark.height;};
            const line_t &line=lines[d.line];
            const sector_t *facing=d.side?line.backsector:line.frontsector;
            unsigned mode=facing?decalMode(*facing):0u;
            auto vertex=[&](float u,float z){return Vertex{span.a.x+tx*u+nx,span.a.y+ty*u+ny,z,texU(u),texV(z),fade,mode};};
            quad(decalBatches[d.mark?d.mark:lump],vertex(u0,z1),vertex(u1,z1),vertex(u1,z0),vertex(u0,z0));
        }
    }
    debris.erase(std::remove_if(debris.begin(),debris.end(),[&](const Debris &p){return now-p.born>p.life||now<p.born;}),debris.end());
    // Debris moves at the tic rate, like the rings, stretches upright while
    // it falls fast and shrinks once before it is gone.
    for(const auto &p:debris) {
        float t=std::floor((now-p.born)*TICRATE)/TICRATE,x=p.x+p.vx*t,y=p.y+p.vy*t,z=p.z+p.vz*t-210*t*t;
        const sector_t *sector=R_PointInSubsector((fixed_t)(x*FRACUNIT),(fixed_t)(y*FRACUNIT))->sector;
        float floor=floorZ(sector)+0.2f;
        size_t index=sector-sectors;
        if(z<floor&&index<sectorMist.size()&&sectorMist[index].liquid&&!sectorMist[index].thick)continue; // Back into the water.
        z=std::min(z,ceilZ(sector)-p.size*0.5f);
        float size=t>p.life*0.75f?p.size*0.5f:p.size,tall=std::abs(p.vz-420*t)>220?2.0f:1.0f;
        // A splinter, long, tumbling: turned end over end every other tic.
        if(p.stretch>0) {tall=p.stretch;if((int)(t*TICRATE/2)&1) {size*=p.stretch;tall=1/p.stretch;}}
        // Chips off a wall in the light round them, as the blood (by the sector's level alone they showed their bare
        // colour, near black in a bright room; the user); sparks and fire their own.
        std::array<float,3> lit;
        if(p.lit)lit=particleColour(p.color,x,y,std::max(z,floor),false);
        else for(int c=0;c<3;++c)lit[c]=std::min(1.0f,p.color[c]*p.light);
        // Lit chips skip the palette snap: it has next to no tinted greys, so green spill on a grey chip snapped
        // back to plain grey.
        pixelBlock(particleVertices,camera,x,y,std::max(z,floor),size,tall,lit,z<=floor+p.size*0.5f,&p.color,!p.lit,nullptr,1,1,farLeast);
    }
    buildWallHits(camera);
    buildExplosionSpecks(camera);   // a blast's sparks and dust (explosions.cpp)
}
// The first wall a ray from (x,y,z) along the unit horizontal (dx,dy) meets
// within length, climbing dz per unit: one-sided lines, and two-sided ones
// where the ray passes above the far ceiling or below the far floor, at the
// current heights. side faces the ray's origin; section is as in wallSpan.
// only: the lines to look at (all when null).
bool firstSolidWall(float x,float y,float z,float dx,float dy,float dz,float length,WallHit &hit,const std::vector<int> *only) {
    std::vector<std::pair<float,int>> crossings;
    int count=only?(int)only->size():numlines;
    for(int k=0;k<count;++k) {
        int i=only?(*only)[k]:k;
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        float det=dx*ey-dy*ex;if(std::abs(det)<1e-6f)continue;
        float ox=ax-x,oy=ay-y,t=(ox*ey-oy*ex)/det,u=(ox*dy-oy*dx)/det;
        if(t>0.01f&&t<length&&u>=0&&u<=1)crossings.push_back({t,i});
    }
    std::sort(crossings.begin(),crossings.end());
    for(auto [t,i]:crossings) {
        const line_t &line=lines[i];
        float ax=units(line.v1->x),ay=units(line.v1->y),ex=units(line.v2->x)-ax,ey=units(line.v2->y)-ay;
        int side=ex*(y-ay)-ey*(x-ax)>=0?1:0;
        const sector_t *front=side?line.backsector:line.frontsector,*back=side?line.frontsector:line.backsector;
        if(!front)continue;
        float at=z+dz*t;int section=-1;
        if(!back)section=0;
        else if(at>=ceilZ(back)&&!(front->ceilingpic==skyflatnum&&back->ceilingpic==skyflatnum))section=1;
        else if(at<=floorZ(back))section=2;
        if(section<0)continue;
        hit={i,side,section,t};return true;
    }
    return false;
}
float valueNoise(float x,float y,float cell,uint32_t seed) {
    x/=cell;y/=cell;int ix=(int)std::floor(x),iy=(int)std::floor(y);
    float fx=x-ix,fy=y-iy;fx=fx*fx*(3-2*fx);fy=fy*fy*(3-2*fy);
    float a=hashUnit(ix,iy,seed),b=hashUnit(ix+1,iy,seed),c=hashUnit(ix,iy+1,seed),d=hashUnit(ix+1,iy+1,seed);
    float top=a+(b-a)*fx,bottom=c+(d-c)*fx;
    return top+(bottom-top)*fy;
}
// Stepped with the game's wrap (carried.cpp), as the blood is.
void impactsCarried(float dx,float dy,float dz) {
    for(auto &d:wallDecals)d.line=lineCarried(d.line,dx,dy);
    for(auto &d:flatDecals) {d.x+=dx;d.y+=dy;d.sector=sectorCarried(d.x,d.y);}
    for(auto &d:scorches) {d.x+=dx;d.y+=dy;d.sector=sectorCarried(d.x,d.y);}
    for(auto &p:debris) {p.x+=dx;p.y+=dy;p.z+=dz;}
    wallDecals.erase(std::remove_if(wallDecals.begin(),wallDecals.end(),[](const WallDecal &d){return d.line<0;}),wallDecals.end());
    flatDecals.erase(std::remove_if(flatDecals.begin(),flatDecals.end(),[](const FlatDecal &d){return d.sector<0;}),flatDecals.end());
    scorches.erase(std::remove_if(scorches.begin(),scorches.end(),[](const Scorch &d){return d.sector<0;}),scorches.end());
}
void impactsShutdown() {wallDecals.clear();flatDecals.clear();debris.clear();scorches.clear();wallHitsShutdown();}
} // namespace doom3d
