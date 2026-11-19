// graphics.cfg's version line (settings_panel.h): a file of an older release is skipped, one
// of this release or named on the command line is read, and every file written starts with it.
#include <cstdio>
#include <cstring>
#include "../platform/settings_panel.h"

using namespace doom3d;

static int sunDiscAfter(const char *text,bool anyVersion) {
    Settings s{};
    s.sunDisc=0;
    char line[128];
    std::snprintf(line,sizeof(line),"%s",text);
    if(anyVersion)readAnyVersion();
    for(char *row=std::strtok(line,"\n");row;row=std::strtok(nullptr,"\n")) {
        char key[64];float value;
        if(std::sscanf(row,"%63s %f",key,&value)==2)readOption(s,key,value);
    }
    finishReading(s);
    return s.sunDisc;
}

extern "C" int LauncherTests_ConfigVersion(void) {
    if(sunDiscAfter("sun_disc 1\n",false)!=0)return 0;                       // an older release's file
    if(sunDiscAfter("config_version 0\nsun_disc 1\n",false)!=0)return 0;     // a version that isn't this one
    if(sunDiscAfter("config_version 1\nsun_disc 1\n",false)!=1)return 0;     // this release's
    if(sunDiscAfter("sun_disc 1\n",true)!=1)return 0;                        // -graphics: as it is
    if(sunDiscAfter("sun_disc 1\n",false)!=0)return 0;                       // and the check is back after it
    FILE *file=std::tmpfile();
    if(!file)return 0;
    Settings s{};
    writeOptions(file,s);
    std::rewind(file);
    char first[64]={0};
    bool ok=std::fgets(first,sizeof(first),file)&&!std::strcmp(first,"config_version 1\n");
    std::fclose(file);
    return ok;
}
