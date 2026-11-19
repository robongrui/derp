/* Aim the sun (bindings: Aim the sun, Sun back). The first puts the sun where the crosshair
   points, light, shadows and the disc, re-baked at once as a change of hour is, and keeps it in
   the player's derpinfo.txt (derp_info_file.c) for this map and F4's hour: sun = angle, height
   at the map's own hour, sun.dusk = ... at a picked one (sun.cpp's placeSun reads both). Whole
   degrees, so what shows is what the file says, and a mapper can copy the line into the WAD's
   DERPINFO as it is. The second takes this map and hour's line out again: the sun is the WAD's
   or the game's once more. With the dev tools, in DERP mode on one of the game's own maps, the
   aims are held for its authored look instead (devtools/authoring.cpp, devSunHold). */
#include "internal.h"
#include <cmath>

namespace doom3d {
namespace {
float degrees(float radians) {return radians*180/(float)doomPi;}
float wrapped(float angle) {angle=std::fmod(angle,360.0f);return angle<0?angle+360:angle;}

void say(const char *text) {
    static char message[96];
    snprintf(message,sizeof message,"%s",text);
    players[consoleplayer].message=message;
    printf("%s\n",message);fflush(stdout);
}

// The player's file read anew, and the sun with it.
void placeAgain() {
    DerpInfo_Level(DerpInfo_MapName);
    sunPlaceAgain();
}
}

// As a change of hour does it (scene.cpp).
void sunPlaceAgain() {
    sunBaked=false;lightBakeKey=-1;bounceKey=-1;ambientKey=-1;flowKey=-1;
    weatherSunAgain();
}
}
using namespace doom3d;

void I_Render3DSunAim(int back) {
    player_t &player=players[displayplayer];
    if(gamestate!=GS_LEVEL||!player.mo||!DerpInfo_MapName[0])return;
    int hour=climateHour();
    const char *hourName=DerpInfo_HourName(hour);
    char key[24],here[48];
    if(hourName) {snprintf(key,sizeof key,"sun.%s",hourName);snprintf(here,sizeof here,"%s at %s",DerpInfo_MapName,hourName);}
    else {snprintf(key,sizeof key,"sun");snprintf(here,sizeof here,"%s",DerpInfo_MapName);}
    char text[96];
    int had=0;
    float angle=wrapped(std::round(degrees((float)player.mo->angle*(float)(2*doomPi/4294967296.0))));
    float height=std::clamp(std::round(degrees(tourpitch)),1.0f,89.0f);
    if(hour==TIME_NIGHT) {say("no sun at night: pick another hour in F4");return;}
    if(back) {
#ifdef DOOM_DEVTOOLS
        if(devSunHold(hour,true,angle,height))return;
#endif
        if(!DerpInfo_FileSet(DERPINFO_PLAYER_FILE,DerpInfo_MapName,key,nullptr,&had)) {say("couldn't write " DERPINFO_PLAYER_FILE);return;}
        snprintf(text,sizeof text,had?"%s: the map's sun again":"%s: the sun is the map's",here);
        say(text);
        if(had)placeAgain();
        return;
    }
    if(!settings.sun||sunLevel<=0||!levelMood().sun) {say("no sun at this hour: pick one in F4");return;}
#ifdef DOOM_DEVTOOLS
    if(devSunHold(hour,false,angle,height))return;
#endif
    char value[32];
    snprintf(value,sizeof value,"%.0f, %.0f",angle,height);
    if(!DerpInfo_FileSet(DERPINFO_PLAYER_FILE,DerpInfo_MapName,key,value,&had)) {say("couldn't write " DERPINFO_PLAYER_FILE);return;}
    snprintf(text,sizeof text,"%s: %s = %s",here,key,value);
    say(text);
    // For the log: how far off the sky's painted sun.
    float painted=wrapped(degrees(sunPaintedYaw));
    printf("  (%+.0f off the painted sun at %.0f)\n",std::remainder(angle-painted,360.0f),painted);fflush(stdout);
    placeAgain();
}
