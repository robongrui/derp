/* Paint lit as a lamp. Some flats are plain coloured paint (Game_PaintLamp:
   FLOOR1_6, two dark reds) that a mapper sometimes lights as a fixture: E2M1's
   inverted cross is FLOOR1_6 at 255 between rooms at 192, and in Doom's own
   renderer it reads as a glowing red sign. Elsewhere the same paint is just a
   red surface (E3M7's ceilings, at their rooms' levels). The mapper's light
   level tells the two apart: the paint glows where its sector is lit well
   above the rooms beside it, and stays paint where it is not. */
#include "internal.h"
#include "../game_materials.h"

namespace doom3d {
namespace {
constexpr int brightLevel=192;   // a fixture is lit at least this high
constexpr int aboveRooms=32;     // and this far above the brightest room beside it
std::vector<char> lit;           // per sector: 1 floor, 2 ceiling
unsigned judged=~0u;

bool paintFlat(int pic) {
    if(pic==skyflatnum||pic<0||pic>=numflats)return false;
    char name[9];memcpy(name,lumpinfo[firstflat+pic].name,8);name[8]=0;
    for(char &c:name)c=(char)toupper((unsigned char)c);
    return Game_PaintLamp(name,1)!=0;
}
// Judged at the level's first look, from the levels the mapper set: a light
// switched later does not turn the paint into a lamp or back.
void judge() {
    judged=r_levelserial;
    lit.assign(numsectors,0);
    std::vector<char> painted(numsectors,0);
    for(int i=0;i<numsectors;++i)
        painted[i]=(char)((paintFlat(sectors[i].floorpic)?1:0)|(paintFlat(sectors[i].ceilingpic)?2:0));
    std::vector<int> brightestBeside(numsectors,-1);
    for(int i=0;i<numlines;++i) {
        const line_t &line=lines[i];
        if(!line.frontsector||!line.backsector||line.frontsector==line.backsector)continue;
        int a=(int)(line.frontsector-sectors),b=(int)(line.backsector-sectors);
        // The fixture's own pieces (the cross's arms) are not the room it lights.
        if(painted[a]&&painted[b])continue;
        brightestBeside[a]=std::max(brightestBeside[a],(int)sectors[b].lightlevel);
        brightestBeside[b]=std::max(brightestBeside[b],(int)sectors[a].lightlevel);
    }
    // A piece that touches only other pieces takes the brightest room its fixture touches.
    for(bool spread=true;spread;) {
        spread=false;
        for(int i=0;i<numlines;++i) {
            const line_t &line=lines[i];
            if(!line.frontsector||!line.backsector)continue;
            int a=(int)(line.frontsector-sectors),b=(int)(line.backsector-sectors);
            if(!painted[a]||!painted[b])continue;
            for(auto [to,from]:{std::pair{a,b},std::pair{b,a}})
                if(brightestBeside[to]<0&&brightestBeside[from]>=0) {brightestBeside[to]=brightestBeside[from];spread=true;}
        }
    }
    for(int i=0;i<numsectors;++i) {
        int level=sectors[i].lightlevel;
        if(!painted[i]||level<brightLevel||brightestBeside[i]<0||level<brightestBeside[i]+aboveRooms)continue;
        lit[i]=painted[i];
    }
}
}

bool litPaint(int sector,bool ceiling) {
    if(judged!=r_levelserial||(int)lit.size()!=numsectors)judge();
    return sector>=0&&sector<(int)lit.size()&&(lit[sector]&(ceiling?2:1));
}
Image &sectorFlatImage(int sector,bool ceiling) {
    const sector_t &s=sectors[sector];
    int pic=ceiling?s.ceilingpic:s.floorpic,lump=firstflat+flattranslation[pic];
    return litPaint(sector,ceiling)?litPaintFlat(lump):lumpImage(lump,true);
}
int sectorFlatKey(int sector,bool ceiling) {
    const sector_t &s=sectors[sector];
    int pic=ceiling?s.ceilingpic:s.floorpic,lump=firstflat+flattranslation[pic];
    return litPaint(sector,ceiling)?litPaintKey(lump):-1-lump;
}
}
