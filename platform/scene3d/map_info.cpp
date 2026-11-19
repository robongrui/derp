/* The map's DERPINFO as the renderer goes by it: in DERP mode (F4 Gameplay, settings.authored)
   the look picked by hand for one of the game's own maps (Game_AuthoredInfo) lies at the bottom,
   and the WADs' lumps and the player's derpinfo.txt (DerpInfo_Map) over it, key by key. The
   game's own rules (Phobos, gravity, nukage's harm, the library) read DerpInfo_Map alone:
   the authored look is drawing only. */
#include "internal.h"

namespace doom3d {
namespace {
derp_map merged;
unsigned version=0,authoredAgain=0;
}

void mapInfoAgain() {++authoredAgain;}

const derp_map &mapInfo() {
    static unsigned level=~0u,derp=~0u,again=~0u;static int authored=-1;
    if(level!=r_levelserial||derp!=DerpInfo_Serial||again!=authoredAgain||authored!=settings.authored) {
        level=r_levelserial;derp=DerpInfo_Serial;again=authoredAgain;authored=settings.authored;
        DerpInfo_Unset(&merged);
        size_t length=0;const char *file=nullptr;
        const char *text=settings.authored?Game_AuthoredInfo(gameepisode,gamemap,&length,&file):nullptr;
        std::string live;
#ifdef DOOM_DEVTOOLS
        // The dev tools read the file in the source tree as it stands, saves included.
        if(text&&file&&devAuthoredText(file,live)) {text=live.c_str();length=live.size();}
#endif
        if(text)DerpInfo_Parse(text,length,DerpInfo_MapName,file?file:"authored",&merged);
        DerpInfo_Over(&merged,&DerpInfo_Map);
#ifdef DOOM_DEVTOOLS
        devAuthoredHeld(merged);   // aims held, not yet saved
#endif
        ++version;
    }
    return merged;
}

unsigned mapInfoVersion() {mapInfo();return version;}
}
