/* The physical explosion's grid (explosion_grid.cpp simulates it, explosion_view.cpp draws it). */
#pragma once
#include "internal.h"

namespace doom3d {
struct BlastGrid {
    static constexpr int nx=48,ny=48,nz=28,cells=nx*ny*nz;
    static constexpr float cell=8;                    // map units a cell
    float x0=0,y0=0,z0=0;                             // its low corner in the world
    float bx=0,by=0,bz=0;                             // the blast, in cells
    bool fuel=false,dry=true;float scale=1;uint32_t seed=0;int tics=0;bool live=true;
    std::vector<uint8_t> solid,reached,nearSolid;     // the map in it; what the blast can get to; where a sample has a solid corner
    std::vector<float> u,v,w,heat,smoke,dust,p,div,a,b,c;   // cells a tic; a, b, c: scratch
    std::vector<std::array<float,3>> light;           // the baked light, a column
    std::vector<std::array<float,3>> fillWay;size_t fillDone=0;   // each of those cells' way out, and how many are alight
    std::vector<int> fillOrder;int fillReach=0;       // the open cells nearest the blast by the way round, nearest first; how far off the first of them lie
    std::vector<std::array<float,2>> between;         // a column's own floor and ceiling (the sky: far up), to the unit
    std::array<float,3> dustColour={0.5f,0.47f,0.43f};
    float heatSum=0,hotAt[3]={};                      // how much burns, and where (world)
    float massAt[3]={};                               // the middle of all of it: fire, smoke and dust
    int lo[3]={0,0,0},hi[3]={-1,-1,-1};               // the cells holding anything
    int fireKey=0,smokeKey=0,drawnStep=-1;bool fire=false,smoke_=false;
    BlastGridCard card={};
    static int at(int i,int j,int k) {return i+nx*(j+ny*k);}
};
const float *blastNoise();          // explosion_fire.cpp: 64 x 64, tiling
const byte *blastFireRamp();        // six fire colours, hottest first
} // namespace doom3d
