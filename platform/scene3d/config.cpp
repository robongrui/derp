/* Reading and writing graphics.cfg; the options and their keys are listed
   once, in settings_panel.cpp. */
#include "internal.h"
#include "../settings_panel.h"

namespace doom3d {
const char *graphicsConfig="graphics.cfg";
bool graphicsConfigNamed=false;
void loadSettings() {
    settings.heroLight=Game_HeroLight(); // per game, until the file says otherwise
    FILE *file=fopen(graphicsConfig,"r"); if(!file) return;
    // The player's own file must be of this release (configVersion); one named by -graphics is read as it is.
    if(graphicsConfigNamed) readAnyVersion();
    char key[64];float value;
    while(fscanf(file,"%63s %f",key,&value)==2) readOption(settings,key,value);
    finishReading(settings);
    fclose(file);
}
void saveSettings() {
    FILE *file=fopen(graphicsConfig,"w"); if(!file)return;
    writeOptions(file,settings);
    fclose(file);
}
void settingsChanged() {applySettings();saveSettings();ticSaved=false;++settingsSerial;}
} // namespace doom3d
