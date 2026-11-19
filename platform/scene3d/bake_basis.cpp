/* Directional baked light (Settings::bakedBumps, world/bake.glsl bakedBasis):
   as Half-Life 2's radiosity normal mapping does, the baked light reaching a
   surface is split over three directions tilted out of it 120 degrees apart
   (the HL2 basis), so the artwork's bumps and each brick's lean take every
   baked light the way it comes: lamps, light bounced off the walls round it,
   the sky through an opening, a lit floor below. Lamps are split as they are
   baked (gatherStatic, kept here per texel), the rest by the bounce bake's
   rays (bakeBounce); the share of each direction goes into the direction
   maps' second half (x, y of the first two, z 255 marks it there). */
#include "internal.h"

namespace doom3d {
std::vector<std::array<uint16_t,3>> wallLampLobes,flatLampLobes;
namespace {
// The three directions in the surface's frame (along, across, out), as
// world.frag's bakedBasis takes them.
const float lobe[3][3]={{-0.40824829f,0.70710678f,0.57735027f},{-0.40824829f,-0.70710678f,0.57735027f},{0.81649658f,0.0f,0.57735027f}};
// Lamp lobes are kept in 1/1024 steps of light, up to 64.
constexpr float lobeScale=1024;
}
// Adds weight of light arriving from towards (any length) onto the three
// directions of a surface facing normal, in encodeDirection's frame.
void addLobes(const float normal[3],const float towards[3],float weight,float lobes[3]) {
    float length=std::sqrt(towards[0]*towards[0]+towards[1]*towards[1]+towards[2]*towards[2]);
    if(weight<=0||length<=1e-6f)return;
    bool flat=std::fabs(normal[2])>0.5f;
    float t[3]={flat?1.0f:-normal[1],flat?0.0f:normal[0],0},b[3]={0,flat?1.0f:0.0f,flat?0.0f:1.0f};
    float d[3]={(towards[0]*t[0]+towards[1]*t[1]+towards[2]*t[2])/length,(towards[0]*b[0]+towards[1]*b[1]+towards[2]*b[2])/length,
                (towards[0]*normal[0]+towards[1]*normal[1]+towards[2]*normal[2])/length};
    for(int k=0;k<3;++k)lobes[k]+=weight*std::max(0.0f,d[0]*lobe[k][0]+d[1]*lobe[k][1]+d[2]*lobe[k][2]);
}
void keepLampLobes(std::array<uint16_t,3> &kept,const float lobes[3]) {
    for(int k=0;k<3;++k)kept[k]=(uint16_t)std::min(65535L,std::lround(lobes[k]*lobeScale));
}
void lampLobes(const std::array<uint16_t,3> &kept,float lobes[3]) {
    for(int k=0;k<3;++k)lobes[k]+=kept[k]/lobeScale;
}
// The basis texel: the first two directions' shares of the light, and 255
// marking it there; none where no light reaches. Its alpha is not the
// basis': the bounce's flicker share lives there (bake_bounce.cpp's bounceFlicker).
void writeBasis(uint8_t *out,const float lobes[3]) {
    float total=lobes[0]+lobes[1]+lobes[2];
    if(total<=1e-5f) {out[0]=out[1]=out[2]=0;return;}
    out[0]=(uint8_t)std::lround(std::clamp(lobes[0]/total,0.0f,1.0f)*255);
    out[1]=(uint8_t)std::lround(std::clamp(lobes[1]/total,0.0f,1.0f)*255);
    out[2]=255;
}
// The direction maps, with their second halves: walls as wide as the bake
// atlas (basis at atlasWidth on), floors and ceilings in four layers (floor
// and ceiling lamp directions, then floor and ceiling basis).
void uploadDirections() {
    wallDirectionTexture=nullptr;flatDirectionTexture=nullptr;
    if(wallDirectionCells.empty()||flatDirectionCells.empty())return;
    wallDirectionTexture=gpuCreateTexture(GpuFormat::RGBA8,atlasStride,atlasHeight,wallDirectionCells.data(),atlasStride*4);
    flatDirectionTexture=gpuCreateTexture(GpuFormat::RGBA8,mapWidth,mapHeight*4,flatDirectionCells.data(),mapWidth*4);
    if(!wallDirectionTexture||!flatDirectionTexture)I_Error((char*)"Could not allocate surface maps");
}
// The basis from the lamps alone (and the sector's own light, which comes
// from everywhere), for when the bounce bake does not run to add the rest.
void writeLampBasis() {
    if(!settings.bakedBumps||wallLampLobes.empty())return;
    size_t layer=(size_t)mapWidth*mapHeight;
    parallelFor(mapHeight,[&](int y) {
        for(int x=0;x<mapWidth;++x) {
            size_t i=(size_t)y*mapWidth+x;int sector=seamCells[i].own;
            if(sector==noSector)continue;
            float even=lighting(&sectors[sector])*ambientLobe;
            for(int ceiling=0;ceiling<2;++ceiling) {
                float lobes[3]={even,even,even};lampLobes(flatLampLobes[ceiling*layer+i],lobes);
                writeBasis(&flatDirectionCells[4*((2+ceiling)*layer+i)],lobes);
            }
        }
    });
    parallelFor((int)bakeOrder.size(),[&](int n) {
        const BakeStrip &strip=bakeStrips[bakeOrder[n]];
        float even=lighting(&sectors[strip.sector])*ambientLobe;
        for(int row=0;row<strip.height;++row)for(int column=0;column<strip.width;++column) {
            size_t texel=(size_t)(strip.y+row)*atlasStride+strip.x+column;
            float lobes[3]={even,even,even};lampLobes(wallLampLobes[(size_t)(strip.y+row)*atlasWidth+strip.x+column],lobes);
            writeBasis(&wallDirectionCells[4*(texel+atlasWidth)],lobes);
        }
    });
}
}
