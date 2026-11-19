/* Merged blood (PLAN-skin-suit.md 10.3): once a splat has landed its picture
   is painted into a layer of blood on that surface instead of staying a decal
   of its own, so splats that overlap become one stain with one outline (no
   double edges, no darker double layers), and the blood is kept by the area
   it covers, not by how many splats made it: a painted wall stays painted.
   Each wall section and each floor or ceiling has its layer, made only where
   blood lands, in tiles of 32 x 32 texels on the world's grid (blood is drawn
   at a texel a unit there already, blood_art.cpp). A texel keeps how much
   blood is on it: a thin film a light stain, much a dark one, so hit after hit
   in one place gets darker and heavier instead of gaining edges. A tile is
   drawn again only when blood lands on it, and dries from when it was last
   wetted. Runs stay blood.cpp's (each heavy splat's own), pools stay pools.
   A wall's layer is held to the wall's anchor, so it moves with a door or a
   lift as its splats did. Drawing only. */
#include "internal.h"

namespace doom3d {
constexpr int tileSize=32,maxTiles=384;
constexpr int layerKeyBase=-0x74000000;   // image keys clear of the others (blood art's run to about -0x6BD00000)

struct TileKey {
    int surface,section,tint;   // surface: a floor's or ceiling's sector (ceiling: -2-sector), or a line*2+side
    bool wall;int tx,ty;
    bool operator==(const TileKey &o) const {return surface==o.surface&&section==o.section&&tint==o.tint&&wall==o.wall&&tx==o.tx&&ty==o.ty;}
};
struct TileKeyHash {
    size_t operator()(const TileKey &k) const {
        return hash32((uint32_t)k.surface*73856093u^(uint32_t)k.section*19349663u^(uint32_t)k.tint*83492791u^
                      (uint32_t)k.tx*2654435761u^(uint32_t)k.ty*40503u^(k.wall?0x9e3779b9u:0));
    }
};
struct Tile {
    std::array<byte,tileSize*tileSize> amount{};   // rows from the tile's top (higher along y or z first)
    float lastWet=0;bool dirty=true,glows=false;int key=0;
};
static std::unordered_map<TileKey,Tile,TileKeyHash> tiles;
static unsigned layerSerial;
static int keySerial=0;

// A tile's picture gone: from the images and from every batch keyed by it (an empty batch left behind still
// looks its image up, and an uncaught miss there crashed the game).
static void dropImage(int key) {
    if(!key)return;
    images.erase(key);
    for(auto *group:{&batches,&mirrorBatches,&shadowBatches,&decalBatches}) {group->erase(key);group->erase(-1-key);}
}
// What the open ground under a floor tile is covered with while it falls: snow soaks blood in bright, sand
// drinks it dark and grainy; each buries it over time (PLAN-skin-suit.md 10.4). 0 none, 1 snow, 2 sand.
static int groundOf(const TileKey &k) {
    if(k.wall||k.surface<0||!climateNow().weather||k.surface>=numsectors||!underSky(k.surface))return 0;
    Weather w=falling();
    return w==WEATHER_SNOW?1:w==WEATHER_SAND?2:0;
}
static void checkLevel() {
    if(layerSerial==r_levelserial)return;
    bloodLayerShutdown();
    layerSerial=r_levelserial;
}
// Room for one more: the tile dried longest goes first (its image with it).
static void makeRoom() {
    if(tiles.size()<maxTiles)return;
    auto oldest=tiles.begin();
    for(auto it=tiles.begin();it!=tiles.end();++it)if(it->second.lastWet<oldest->second.lastWet)oldest=it;
    dropImage(oldest->second.key);
    tiles.erase(oldest);
}
static int floorDiv(int a,int b) {return a>=0?a/b:-((-a+b-1)/b);}
// Add amount to the texel at grid (gx, gy) (gy up) of a surface.
static void addTexel(TileKey key,int gx,int gy,int amount,float born) {
    key.tx=floorDiv(gx,tileSize);key.ty=floorDiv(gy,tileSize);
    auto found=tiles.find(key);
    if(found==tiles.end()) {makeRoom();found=tiles.emplace(key,Tile{}).first;}
    Tile &t=found->second;
    int c=gx-key.tx*tileSize,r=tileSize-1-(gy-key.ty*tileSize);
    byte &a=t.amount[(size_t)r*tileSize+c];
    a=(byte)std::min(255,a+amount);
    t.lastWet=std::max(t.lastWet,born);t.dirty=true;
}
static void turnedTexel(int orient,float u,float v,float w,float h,float &s,float &t) {
    if(orient&1)u=w-u;
    if(orient&2)v=h-v;
    if(orient&4) {s=v;t=u;} else {s=u;t=v;}
}
// The splat's picture over its box (as gridFloorSplat and gridWallSplat lay it), each covered texel at (gx, gy).
template<class Visit> static void cover(const Image &art,int orient,float x0,float top,Visit visit) {
    bool quarter=orient&4;
    int w=quarter?art.height:art.width,h=quarter?art.width:art.height;
    for(int j=0;j<h;++j)for(int i=0;i<w;++i) {
        float s,t;turnedTexel(orient,i+0.5f,j+0.5f,(float)w,(float)h,s,t);
        int sc=(int)std::floor(s),tr=(int)std::floor(t);
        if(sc<0||tr<0||sc>=art.width||tr>=art.height||!art.pixels[((size_t)tr*art.width+sc)*2+1])continue;
        visit((int)std::lround(x0)+i,(int)std::lround(top)-1-j);
    }
}

void bloodLayerFloor(int sector,bool ceiling,int tint,float x,float y,const Image &art,int orient,int amount,float born) {
    checkLevel();
    bool quarter=orient&4;
    float ax=(float)(quarter?art.top:art.left),ay=(float)(quarter?art.left:art.top);
    TileKey key{ceiling?-2-sector:sector,0,tint,false,0,0};
    cover(art,orient,std::round(x)-ax,std::round(y)+ay,[&](int gx,int gy){addTexel(key,gx,gy,amount,born);});
}
// One texel of blood on a wall section, gu along it and gh up from its anchor (void_patterns.cpp's fans).
void bloodLayerWallTexel(int line,int side,int section,int tint,int gu,int gh,int amount,float born) {
    checkLevel();
    addTexel(TileKey{line*2+side,section,tint,true,0,0},gu,gh,amount,born);
}
void bloodLayerWall(const WallSplat &d,const Image &art,int amount) {
    checkLevel();
    bool quarter=d.orient&4;
    float ax=(float)(quarter?art.top:art.left),ay=(float)(quarter?art.left:art.top);
    // Along the wall and up from its anchor, on whole units (so it moves with the wall).
    TileKey key{d.line*2+d.side,d.section,d.tint,true,0,0};
    cover(art,d.orient,std::round(d.along)-ax,std::round(d.dz)+ay,[&](int gu,int gh){addTexel(key,gu,gh,amount,d.born);});
}

// A tile's picture: each texel with blood, light where it's thin, dark where much has gathered, a little
// grain so it isn't flat.
static void drawTile(const TileKey &k,Tile &t) {
    const auto &shades=bloodPalette(k.tint);
    std::vector<byte> pixels((size_t)tileSize*tileSize*2,0),mask((size_t)tileSize*tileSize,0);
    // Blood on a lamp lets its light through, red (the user): where the surface under a texel glows, the
    // texel glows too, the more the thinner the blood (a clot stays dark).
    bool glows=false;
    WallSpan span{};bool spanned=k.wall&&wallSpan(lines[k.surface/2],k.surface%2,k.section,span);
    for(int i=0;i<tileSize*tileSize;++i) {
        int a=t.amount[i];
        if(!a)continue;
        float fill=std::min(1.0f,a/200.0f);
        // In snow its thin edge feathers into the white, in sand the edge is grains: both broken up.
        int ground=groundOf(k);
        if(ground&&fill<0.3f&&hashUnit(i%tileSize+k.tx*tileSize,i/tileSize+k.ty*tileSize,0x5ad1u)>(ground==1?0.55f:0.45f))continue;
        float shade=0.62f-0.34f*std::sqrt(fill)+(groundOf(k)==1?0.2f:groundOf(k)==2?-0.12f:0.0f)   // (heavy deep red, not brown-black; snow brighter, sand duller)
            +0.08f*(hashUnit(i%tileSize+k.tx*tileSize,i/tileSize+k.ty*tileSize,(uint32_t)k.surface*31u)-0.5f);
        pixels[(size_t)i*2]=shades.empty()?0:shades[(size_t)std::lround(std::clamp(shade,0.0f,1.0f)*(shades.size()-1))];
        pixels[(size_t)i*2+1]=255;
        int gx=k.tx*tileSize+i%tileSize,gy=(k.ty+1)*tileSize-1-i/tileSize;
        byte under=k.wall?(spanned?wallEmissionAt(k.surface/2,k.surface%2,k.section,gx+0.5f,span.anchor+gy+0.5f):0)
                         :flatEmissionAt(k.surface<0?-2-k.surface:k.surface,k.surface<0,gx+0.5f,gy+0.5f);
        // (Not where the lamp has been shot out: the blood on it went on glowing; the user.)
        if(under&&(k.wall?wallShotOutAt(k.surface/2,k.surface%2,k.section,gx+0.5f,span.anchor+gy+0.5f)
                         :flatShotOutAt(k.surface<0?-2-k.surface:k.surface,k.surface<0,gx+0.5f,gy+0.5f)))under=0;
        if(under) {mask[i]=(byte)std::lround(under*(1-0.65f*std::sqrt(fill)));glows=true;}
    }
    if(!t.key)t.key=layerKeyBase+(++keySerial&0x00ffffff);
    dropImage(t.key);
    images.emplace(t.key,upload(tileSize,tileSize,pixels.data(),0,0,glows?mask.data():nullptr));
    t.glows=glows;
    t.dirty=false;
}

void buildBloodLayer(const Uniforms &camera,float now) {
    checkLevel();
    // A lamp shot out: the blood glowing on lamps drawn again (it lets no light through where that one was).
    static size_t lampsShot=0;
    if(lampsShotCount()!=lampsShot) {lampsShot=lampsShotCount();for(auto &entry:tiles)if(entry.second.glows)entry.second.dirty=true;}
    for(auto &entry:tiles) {
        const TileKey &k=entry.first;Tile &t=entry.second;
        if(t.dirty||!t.key||!images.count(t.key))drawTile(k,t);   // (images cleared under it: drawn again)
        const Image &image=images.at(t.key);
        float age=now-t.lastWet,wet,dark;
        bloodLayerAge(age,wet,dark);
        float x0=(float)(k.tx*tileSize),top=(float)((k.ty+1)*tileSize);
        if(k.wall) {
            WallSplat d{};d.line=k.surface/2;d.side=k.surface%2;d.section=k.section;d.along=x0;d.dz=top;d.born=t.lastWet;
            d.size=tileSize;d.stretch=1;d.tint=k.tint;d.merged=true;d.layer=true;
            const line_t &line=lines[d.line];const sector_t *front=sides[line.sidenum[d.side]].sector;
            float wash=bloodLayerWash(climateNow().weather&&weatherNow(WEATHER_RAIN)>0.01f&&underSky(int(front-sectors)),age,dark);
            if(wash<0)continue;
            gridWallSplat(d,t.key,image,DripLooks{},dark,wash,now,camera);
        } else {
            bool ceiling=k.surface<0;int sector=ceiling?-2-k.surface:k.surface;
            if(sector<0||sector>=numsectors)continue;
            if(ceiling?camera.eye[2]>ceilZ(&sectors[sector]):camera.eye[2]<floorZ(&sectors[sector]))continue;   // back of the flat
            // Rained on it washes away; snowed or sanded on it is buried, slower (the same pale-then-gone steps),
            // and sand leaves no shine.
            int ground=ceiling?0:groundOf(k);
            bool covered=ground&&weatherNow(falling())>0.01f;
            float wash=bloodLayerWash(covered||(!ceiling&&bloodLayerRained(sector,x0+tileSize/2,top-tileSize/2)),covered?age/2.5f:age,dark);
            if(ground==2)wet=0;
            if(wash<0)continue;
            // Over the pools (they spread on their own, under it): at the same lift the two fought and shimmered.
            gridFloorSplat(t.key,image,sector,x0,top,0,ceiling,ceiling?0:wet,dark,wash,camera,1.6f,t.glows);
        }
    }
}
void bloodLayerShutdown() {
    for(auto &entry:tiles)dropImage(entry.second.key);
    tiles.clear();
}
} // namespace doom3d
