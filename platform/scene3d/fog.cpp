/* Fog (a WeatherKind; weather.cpp says when it lies and how thick): a bank
   that lies instead of falling. This file also carries the bank to the
   shaders for dry ice (dry_ice.cpp), which lies its own way.
   Out under the sky it pools: each open-air area fills with it to a level
   over its low ground (a quarter of its ground lies lower), over the eye in
   the yards and a layer under the knees on the ledges above them, so from a
   ledge it lies below like a sea. Indoors it goes where the cold goes: a
   room holds as much of it as the drafts (drafts.cpp) make it cold, thinning
   room by room from the openings, and there it hugs the floor. Each sector's
   top, share and light go to the shaders in the sector buffer right after
   the sector entries (fogBankEntries), the shares easing over a few seconds
   as doors open and shut. fog_bank.glsl fades what lies behind it toward the
   fog's colour in the light where the eye and the point stand; what glows
   (Doom's fullbright: lamps, fireballs, eyes) cuts through. Its body is mist
   sheets drifting on the wind (fogSheets, drawn by mist.frag). The ground
   lies damp, the windows mist over (glass.frag), the sky is hidden
   (sky.frag), and it hushes the open air as snow does. Render-only. */
#include "internal.h"
#include "../rooms.h"
extern "C" {
#include "../audio/sound_options.h"
}

namespace doom3d {
namespace {
constexpr float thin=24;          // over open ground above the pool: a layer under the knees
constexpr float pooled=72;        // the pool over its area's low ground: over the eye
constexpr float hugs=40;          // indoors and under porches it hugs the floor, just under the eye
constexpr float density=0.0018f;  // a whole bank takes half the light in 384 units
constexpr float dryDensity=0.012f; // dry ice: half in 58
// Per sector: the open-air pool's level (NAN under a roof), how much of
// the bank lies there now (-1 before the first tic).
std::vector<float> pool,share;
unsigned bankLevel=~0u;
int bankTic=-1;
// The open air's usual light (its sectors by their ground) and the fog's
// colour, for the level.
float skyLight=0.6f;
std::array<float,3> albedo={0.62f,0.64f,0.66f};

bool foggy() {return falling()==WEATHER_FOG;}
bool dry() {return falling()==WEATHER_DRY_ICE;}
// The open air's mean light and the bank's colour for the level: a pale
// grey, a quarter of the way toward the sky's own horizon (brought to the
// grey's brightness); dry ice a fifth brighter, nearly white.
unsigned colorLevel=~0u;Weather colorKind=WEATHER_NONE;
void levelColor() {
    if(colorLevel==r_levelserial&&colorKind==falling())return;
    colorLevel=r_levelserial;colorKind=falling();
    float lit=0;int litCells=0;
    for(const SeamCell &c:seamCells) {
        if(c.own==noSector||!underSky(c.own)||sectors[c.own].floorpic==skyflatnum)continue;
        lit+=lighting(&sectors[c.own]);++litCells;
    }
    skyLight=litCells?lit/litCells:0.6f;
    const std::array<float,3> grey={0.62f,0.64f,0.66f};
    std::array<float,3> horizon=skyHorizon();
    float luma=0.3f*horizon[0]+0.59f*horizon[1]+0.11f*horizon[2],scale=0.64f/std::max(luma,0.02f);
    for(int c=0;c<3;++c)albedo[c]=std::min(1.0f,(grey[c]+(std::min(1.0f,horizon[c]*scale)-grey[c])*0.25f)*(dry()?1.2f:1.0f));
}

// The pools: each yard (the room map's outdoor area) fills to pooled units
// over the height a quarter of its open ground lies below (by map cells, so
// a small pit doesn't sink the whole yard's fog); its porches hug the floor.
void findPools() {
    Rooms_Build();
    pool.assign(numsectors,NAN);
    auto open=[](int s){return underSky(s)&&sectors[s].floorpic!=skyflatnum;};
    std::vector<int> cells(numsectors,0);
    for(const SeamCell &c:seamCells)if(c.own!=noSector)++cells[c.own];
    int yards=Rooms_YardCount();
    std::vector<std::vector<std::pair<float,int>>> ground(yards);
    for(int i=0;i<numsectors;++i)if(open(i)&&cells[i]&&Rooms_Yard(i)>=0)ground[Rooms_Yard(i)].push_back({floorZ(&sectors[i]),cells[i]});
    std::vector<float> level(yards,NAN);
    for(int g=0;g<yards;++g) {
        auto &heights=ground[g];
        if(heights.empty())continue;
        std::sort(heights.begin(),heights.end());
        int total=0,seen=0;
        for(const auto &h:heights)total+=h.second;
        for(const auto &h:heights) {seen+=h.second;if(seen*4>=total) {level[g]=h.first+pooled;break;}}
    }
    for(int i=0;i<numsectors;++i)if(open(i)&&Rooms_Yard(i)>=0)pool[i]=level[Rooms_Yard(i)];
}
// Where the bank tops out over a sector: in the open its area's pool, at
// least a thin layer over the ground; under a roof it hugs the floor. Dry
// ice as deep as it lies there.
float bankTop(int s) {
    if(dry())return dryIceTop(s);
    const sector_t &sector=sectors[s];
    float floor=floorZ(&sector);
    if(underSky(s)&&!std::isnan(pool[s]))return std::max(floor+thin,pool[s]);
    return floor+hugs;
}
float bankShare(int s) {return dry()?dryIceShare(s):std::max(share[s],0.0f);}
bool ready() {
    if(dry())return dryIceReady();
    return bankLevel==r_levelserial&&bankTic>=0&&share.size()==(size_t)numsectors&&pool.size()==(size_t)numsectors&&foggy();
}
}
// The bank's colour (c.settleColor), fog's or dry ice's.
std::array<float,3> bankAlbedo() {
    levelColor();
#ifdef DOOM_DEVTOOLS
    if(devBankRed)return {0.95f,0.08f,0.05f};
#endif
    return albedo;
}

// Each tic: the pools anew on a new level, then each sector's share eased
// toward what the drafts say (all of it outside; a room as cold as it is).
// It has been lying a while when the level starts: the first tic takes the
// shares as they are.
void watchFog() {
    if(gamestate!=GS_LEVEL||levelSerial!=r_levelserial||seamCells.empty())return;
    if(dry()||foggy())levelColor();
    if(dry())watchDryIce();
    if(!foggy())return;
    if(bankLevel!=r_levelserial||share.size()!=(size_t)numsectors) {bankLevel=r_levelserial;bankTic=-1;findPools();share.assign(numsectors,-1.0f);}
    if(leveltime==bankTic)return;
    bankTic=leveltime;
    const float tic=weatherTicSeconds(),ease=tic/4;
    for(int s=0;s<numsectors;++s) {
        float want=draftFogShare(s);
        if(std::isnan(want))want=Rooms_Outside(s)?1.0f:0.0f;
        share[s]=share[s]<0?want:share[s]+(want-share[s])*ease;
    }
}

// Per sector one entry, then two for each stretch of a stream (dry ice).
int fogBankCount() {return ready()?numsectors+2*(int)(dry()?dryIceRibbons().size():0):0;}

// After the sector entries: per sector (top, share + 2 * its stream
// stretches, light, where they start from these entries' first), then the
// stretches (BankRibbon's a and b), each sector's together.
void fogBankEntries(std::vector<std::array<float,4>> &info) {
    if(!fogBankCount()||info.size()!=(size_t)numsectors)return;
    static const std::vector<BankRibbon> none;
    const std::vector<BankRibbon> &ribbons=dry()?dryIceRibbons():none;
    std::vector<int> count(numsectors,0),next(numsectors,0);
    for(const BankRibbon &r:ribbons)++count[r.sector];
    for(int s=0,at=numsectors;s<numsectors;++s) {
        next[s]=at;at+=2*count[s];
        info.push_back({bankTop(s),bankShare(s)+2.0f*count[s],lighting(&sectors[s]),(float)next[s]});
    }
    size_t base=info.size()-numsectors;
    info.resize(base+numsectors+2*ribbons.size());
    for(const BankRibbon &r:ribbons) {info[base+next[r.sector]++]=r.a;info[base+next[r.sector]++]=r.b;}
}

// Where the eye stands: the bank's top, share and light there and its
// density (as thick as it lies now); the fog's colour in the open air's light,
// and where the entries start.
void fogBankUniforms(Uniforms &camera) {
    for(float &v:camera.bank)v=0;
    for(float &v:camera.bankColor)v=0;
    const sector_t *eye=fogBankCount()?weatherSectorAt(camera.eye[0],camera.eye[1]):nullptr;
    if(!eye)return;
    int s=int(eye-sectors);
    camera.bank[0]=bankTop(s);camera.bank[1]=bankShare(s);camera.bank[2]=lighting(eye);
    // Indoors dry ice lies as a film per floor cell (floor_film.cpp): the eye's there.
    if(float film=dry()?filmAt(camera.eye[0],camera.eye[1]):0.0f;film>0) {
        camera.bank[0]=std::max(camera.bank[0],floorZ(eye)+film);camera.bank[1]=std::max(camera.bank[1],std::clamp(film/3,0.0f,1.0f));
    }
    camera.bank[3]=(dry()?dryDensity:density)*fallNow;
    std::array<float,3> color=bankAlbedo();
    for(int c=0;c<3;++c)camera.bankColor[c]=color[c]*skyLight;
    camera.bankColor[3]=(float)numsectors;
    // The sky past the open air's dry ice (sky.frag): from a room, its top in
    // the outside sector nearest the eye, so sky seen low over a courtyard
    // through a window lies under it too (fallColor.w the top, settleColor.w 1
    // to read it); out in it, the eye's own bank. Taken outside too, a
    // rooftop's layer nearer by its middle than the street's turned the whole
    // sky grey a step at a time in DOOM 2's cities. The sky judges it from
    // the eye without the walk's bob: dry ice's top is a few units sharp.
    // Near things keep the real eye.
    float open=dry()&&!Rooms_Outside(s)?dryIceOpenTop(camera.eye[0],camera.eye[1],camera.eye[2]):NAN,bob=0;
    if(const player_t &player=players[displayplayer];player.mo)
        bob=std::clamp(units(player.viewz-player.mo->z-player.viewheight),-16.0f,16.0f);
    camera.settleColor[3]=1;camera.fallColor[3]=(std::isnan(open)?camera.bank[0]:std::max(camera.bank[0],open))+bob;
}

// Dry ice's breath in the open air: the haze in the bank's colour, which
// its faint whiteout (whiteout.cpp, thinning in the wind) fades to, so the
// far streets and the sky soften with the layer at their feet, as a
// blizzard's do; the layer alone stood under a crisp sky. Run after the
// other hazes (scene.cpp).
void dryIceHazeUniforms(Uniforms &camera) {
    if(!dry()||!settings.weather||camera.bank[3]<=0)return;
    float amount=std::min(1.0f,fallNow);
    for(int c=0;c<3;++c)camera.haze[c]+=(camera.bankColor[c]-camera.haze[c])*amount;
}

// The bank's body: mist sheets stacked through it over every floor within
// 1600 units where it lies, four a sector from its floor to under its top,
// each in the fog's colour in its sector's light, as thick as the share
// there. They drift on the wind a step per tic, the lower ones slower (the
// ground holds them back), each its own way across the cloud noise.
void fogSheets(const Uniforms &camera,std::vector<MistTriangle> &out) {
    if(!fogBankCount())return;
    if(dry())return;   // dry ice is all in the stepped fog
    float now=std::floor(gameSeconds()*TICRATE)/TICRATE;
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i];if(poly.size()<3)continue;
        const sector_t &sector=*subsectors[i].sector;
        int s=int(&sector-sectors);
        if(sector.floorpic==skyflatnum||share[s]<0.05f)continue;
        const auto &bounds=floorBounds[i];
        if(camera.eye[0]<bounds[0]-1600||camera.eye[0]>bounds[2]+1600||
           camera.eye[1]<bounds[1]-1600||camera.eye[1]>bounds[3]+1600)continue;
        float bottom=floorZ(&sector),top=std::min(bankTop(s),ceilZ(&sector)-2);
        if(top-bottom<8)continue;
        bool open=underSky(s);
        float light=lighting(&sector),alpha=0.1f*share[s]*std::min(1.0f,fallNow*1.25f)*(open?1.0f:0.8f);
        constexpr int layers=4;
        for(int layer=0;layer<layers;++layer) {
            float z=bottom+(top-bottom)*(layer+1)/(layers+1),drift=0.2f+0.08f*layer;
            float u=layer*0.31f-windNow[0]*drift*now/160,v=layer*0.17f-windNow[1]*drift*now/160;
            for(size_t n=1;n+1<poly.size();++n) {
                MistTriangle triangle={};int k=0;
                for(Point p:{poly[0],poly[n],poly[n+1]}) {
                    triangle.v[k++]={p.x,p.y,z,u,v,alpha*(1.0f-layer*0.1f),0,albedo[0]*light,albedo[1]*light,albedo[2]*light};
                    triangle.depth+=(p.x-camera.eye[0])*camera.forward[0]+(p.y-camera.eye[1])*camera.forward[1]+(z-camera.eye[2])*camera.forward[2];
                }
                out.push_back(triangle);
            }
        }
    }
}

void fogShutdown() {pool.clear();share.clear();bankLevel=~0u;bankTic=-1;colorLevel=~0u;dryIceShutdown();}

namespace {
// How damp the ground lies: in patches, never wet through, the open ground
// a little more than the lee.
float fogGround(const WeatherGround &cell) {
    float n=valueNoise(cell.x,cell.y,72,cell.seed);
    // scp_7179_b is real
    return cell.lee?0.12f:n<0.5f?0.12f:0.35f;
}
// It hushes the open air as snow does, half as much.
void fogHush(float amount) {I_AmbienceSnow(amount*0.5f);}
}
// Fog drifts nearly flat (its damp reaches in under the roofs' edges), mists
// the glass over in about 40 seconds and dampens the sill under a broken
// window in a minute. It is cool out in it; it lays no trails through the
// rooms, only fills them.
const WeatherKind fogWeather={"fog",0.5f,40,60,false,fogGround,nullptr,fogHush,nullptr,4,false,nullptr,bankAlbedo,false};
} // namespace doom3d
