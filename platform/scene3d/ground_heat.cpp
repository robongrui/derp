/* The heat of the day (climate.cpp, from 30 C) shimmering over the open
   ground: a sheet of hot air 20 units over every floor under the sky, its
   heat nothing near the eye, full from about 1300 units out and fading
   again toward 2400, so the far ground and the feet of what stands on it
   waver in the raster shimmer the lava's heat haze uses (present.frag). The
   wind carries the hot air off: a gale leaves a quarter of it. Not over
   liquids. Render-only. */
#include "internal.h"

namespace doom3d {
void buildGroundHeat(const Uniforms &camera) {
    float heat=climateHeat()*(1-0.75f*std::clamp(windSpeedNow()/230,0.0f,1.0f));
    if(heat<0.05f||camera.effects[2]!=0||sectorMist.size()!=(size_t)numsectors)return;
    constexpr float rise=20,reach=2400;
    auto vertex=[&](float x,float y,float z) {
        float d=std::hypot(x-camera.eye[0],y-camera.eye[1]);
        float out=std::clamp((d-256)/1024,0.0f,1.0f)*std::clamp((reach-d)/600,0.0f,1.0f);
        return Vertex{x,y,z,0,0,0.8f*heat*out,0};
    };
    for(int i=0;i<numsubsectors;++i) {
        const auto &poly=floors[i];if(poly.size()<3)continue;
        const sector_t &sector=*subsectors[i].sector;
        int index=int(&sector-sectors);
        if(sector.floorpic==skyflatnum||sectorMist[index].liquid||!underSky(index))continue;
        const auto &b=floorBounds[i];
        if(camera.eye[0]<b[0]-reach||camera.eye[0]>b[2]+reach||camera.eye[1]<b[1]-reach||camera.eye[1]>b[3]+reach)continue;
        float top=std::min(ceilZ(&sector),floorZ(&sector)+rise);
        if(top<=floorZ(&sector)+2)continue;
        for(size_t n=1;n+1<poly.size();++n)for(Point p:{poly[0],poly[n],poly[n+1]})heatVertices.push_back(vertex(p.x,p.y,top));
    }
}
} // namespace doom3d
