# Play DERP

## Starting a game

Start the launcher: **DERP.exe** on Windows, **derp** on Linux, **Play
DERP.app** on the Mac (starting `Play DERP` itself opens it too). Nothing needs
installing.

The launcher lists every game WAD it finds: in the `wads` folder next to it, in
the folders named by `DOOMWADDIR` and `DOOMWADPATH`, and in DOOM and Heretic
games installed through Steam or GOG. The included Freedoom is always there.
Pick a game on the left and press **Play** (or Enter). Heretic WADs start the
Heretic program by themselves.

Drop files on the launcher's window or its icon and each goes where it
belongs:

| Dropped | Goes to |
| --- | --- |
| A game WAD | The games list |
| Custom WADs, `.deh` and `.bex` patches | The load list, in the order they load. A patch named like a WAD comes along with it. |
| PlayStation, 3DO and re-release music packs, music-only WADs | The music |
| `.sf2` SoundFonts | The SoundFont |

A whole folder works too. Files are never copied: the launcher remembers where
they are, and each game WAD keeps its own load list. DOOM II maps need DOOM II
or Freedoom: Phase 2, and DOOM episode maps need DOOM or Freedoom: Phase 1; the
launcher says so before starting and offers the right one.

The launcher's **Settings** page has the same options as F4 in the game.
**Reset to defaults** there fixes a setting that keeps the game from starting.
The launcher works with a gamepad.

With the source code instead of a download, build it first as described in
[BUILDING.md](BUILDING.md).

### Custom maps

Maps made for vanilla DOOM or limit-removing ports work, with their DEHACKED
patches. A patch inside the WAD is applied automatically, and a `.deh` or
`.bex` file named like the WAD next to it is loaded with it. Maps for Boom,
MBF21, GZDoom or other extended engines are not supported. SIGIL loaded on
The Ultimate DOOM plays as episode 5.

In the game, press **Esc**, choose **New Game**, an episode and a difficulty.

## Controls

| Action | Keyboard and mouse |
| --- | --- |
| Move | W A S D |
| Turn | Mouse, or ← → |
| Look up and down | Mouse |
| Fire | Left mouse button, or Ctrl |
| Use, open doors | Space |
| Run | Hold Shift |
| Jump | E |
| Crouch, slide | C |
| Kick | Q |
| Shootslide | Right mouse button |
| Slow motion | X, or F12 |
| Weapons | 1–8, mouse wheel |
| Flashlight | F |
| Automap | Tab |
| Menu, release the mouse | Esc |
| Save, load | F2, F3 |
| Quicksave, quickload | F6, F9 |
| Settings | F4 |
| Screenshot | F7 |
| Show FPS, step through | The key left of 1 |
| Fullscreen | F11 |
| Cheat menu (fly, no monsters, map warp) | F5 |
| Quit | Command-Q on the Mac |

Crouch, slide, kick and the shootslide work in DERP mode; jump works there and
on Phobos. The slow motion key needs **F4 → Graphics → Combat → Slow motion**
set to Key. In DERP mode use Key, not When shooting, which gets in the way of
DERP mode's own slow motion: there the key runs on the hourglass beside the
status bar, and the shootslide works whatever the setting is. On keyboards with
media keys, hold **Fn** for the F keys.

Every action can take two keys or mouse buttons and one gamepad button under
**F4 → Controls → Bindings**. Mouse buttons 4 and 5 and the wheel can be bound,
and a wheel notch can pick a weapon slot. Sound volume and mouse sensitivity
are under **Options** in the game menu.

### Gamepads and the Steam Deck

Plug in a gamepad (Xbox, PlayStation, Switch Pro, 8BitDo, the Steam Deck's own
controls and most others) and play. The left stick moves as fast as it is
tilted, and the right stick turns and looks like the mouse.

| Action | Gamepad |
| --- | --- |
| Move | Left stick |
| Turn and look | Right stick (held all the way over, it turns faster) |
| Fire | RT |
| Use | X |
| Jump | A |
| Crouch, slide | B |
| Kick | Y |
| Shootslide | L3 |
| Flashlight | LT |
| Next, previous weapon | RB, LB |
| Center view | R3 |
| Gyro aim while held | R4 |
| Automap | Back (in the map, LB and RB zoom) |
| Menu | Start |
| Settings (F4) | Hold Back, press Start |

In the menus the d-pad or left stick moves, **A** picks and **B** goes back;
they also answer yes and no. Picking a save slot with the pad names the save
after the map and the time, so **A** saves at once. In Heretic, A and B fly up
and down, the d-pad's left and right walk the inventory, LT uses the artifact
and Y the Tome of Power.

**F4 → Controls** has turn and look speed, the stick dead zone, the turn
boost, swapped sticks, gyro aim (DualSense, DualShock 4, Switch Pro, and the
Deck with Steam Input off; always on, or only while R4 is held) and rumble.
Joysticks SDL has no pad layout for work as DOOM's own digital joystick.

On a **Steam Deck** the game starts fullscreen (`-window` keeps a window). Add
it to Steam as a non-Steam game, with the WAD beside it or `-iwad` in its
launch options, and pick Steam's **Gamepad** controller layout. The
**Handheld** preset in F4 is made for it.

### Movement and aim

DERP's movement starts and stops sooner than DOOM's, glides less after you let
go, and moves no faster diagonally than straight ahead. Walking and running
keep their top speeds. **F4 → Game → Play → Movement** offers:

- **Normal**, as above.
- **Normal plus**: holding a running diagonal for about half a second builds
  up to DOOM's faster strafe-running.
- **Classic**: DOOM's own movement. `-classicmovement` picks it for a session.

DERP mode runs slower than DOOM, at Quake's pace. Demos and netgames always
move as in DOOM.

With free look, bullets and missiles go where you look, so you can miss.
**F4 → Controls → Aim → Classic aim** brings back DOOM's autoaim, following
the view only when nothing is in front. That is the easier aim on a gamepad.
Demos and netgames aim as in vanilla. The same page has **Invert look**, the
crosshair and **Weapon bob** (Classic, Sync to footsteps or Off).

## The settings panel

**F4** opens the settings over the paused, dimmed game. Options are sorted
into four groups, each with tabs:

| Group | Tabs |
| --- | --- |
| Game | World (gameplay mode, gore, weather and climate), Phobos, Play (movement, saving) |
| Controls | Aim, Gamepad, Rumble, Bindings |
| Graphics | Display, Surfaces, Lighting, Live light, Sky and air, Liquids, Combat |
| Sound | Music (music and effects playback), Made-up sounds, Hearing |

The highlighted option's description is shown at the bottom. Options that only
refine another are greyed out while it is off, a small diamond marks each
option changed from its default, and bars beside an option show what it costs
in frame time. **Graphics → Display → Preset** sets every costly option at
once (Lowest, Handheld, Balanced, High, Everything) and leaves matters of
taste alone. The [README](README.md) explains what the options do.

| Key | Does |
| --- | --- |
| Up, Down, mouse wheel | Choose an option |
| Left, Right, Return | Change it |
| Tab, Shift+Tab | Next or previous page, on through every group |
| PgUp, PgDn | Previous or next group |
| 1, 2, ... | Jump to a tab in the group |
| Backspace | Reset one option |
| R | Restore all defaults |
| Esc or F4 | Close and apply |

The mouse works too: click an option to change it (right-click goes
backwards), click a slider to set it, and click a tab to open it.

Settings are saved in `graphics.cfg`, one `key value` per line. Each option's
key is listed beside it in
[`platform/settings_panel.cpp`](platform/settings_panel.cpp); switches are
`0` or `1`. F4 and F7 replace the original sound-menu and end-game shortcuts,
which are still in the game menu.

## Where files go

Saves, settings, screenshots (in `Screenshots`) and the log (`game.log`) live
in a `Local Games/DOOM` folder in your user's application data:

| System | Folder |
| --- | --- |
| Windows | `%APPDATA%\Local Games\DOOM\` |
| macOS | `~/Library/Application Support/Local Games/DOOM/` |
| Linux | `~/.local/share/Local Games/DOOM/` |

Heretic keeps its own `Local Games/Heretic` folder. Saves belong to this build;
old DOS and Linux DOOM saves don't load.

## Music and sound packs

Music plays from the bundled GeneralUser GS SoundFont. Put another `.sf2` in
the `soundfonts` folder beside `doom.cfg` and it is used instead (the first
one alphabetically), or launch with `-soundfont bank.sf2`. **F4 → Sound →
Music** switches to OPL, an emulated FM chip with the WAD's own instruments,
as on a DOS Sound Blaster, and on the Mac to the system synthesizer.

**Soundtrack** plays recordings instead, from packs placed beside your game
WAD, in the `wads` folder or dropped on the launcher: `HQPSXMUS.WAD` for the
PlayStation soundtrack, the `*_3DO.wad` files for 3DO, and the 2024
re-release's `extras.wad` for the IDKFA remix and the recorded originals.

DERP mode's soldiers speak from `derp-voices.wad`, a separate download from the
[v0.8 release](https://github.com/robongrui/derp/releases/tag/v0.8) placed beside the game WAD. Without it they only cry out. The voice packs were
generated with ElevenLabs and have their own licence,
[`barks/VOICES-LICENSE.txt`](barks/VOICES-LICENSE.txt): use them with DERP and
pass them on unmodified, but don't pull their sounds out to use on their own.

The ambience is quiet on purpose. To hear it under the music, turn the music
down in **Options → Sound Volume**.

## Custom glow masks

The renderer finds glowing pixels in the artwork by itself. To override it
for one texture, put a greyscale PNG in `Emissive/walls/NAME.png` or
`Emissive/flats/NAME.png` beside `graphics.cfg`, named with the uppercase WAD
texture name and at the texture's exact size. White pixels glow, black ones
are lit normally, grey ones glow partly, and transparent ones don't glow. An
all-black mask turns glow off for that texture. Restart after changing masks.
`-emissive /absolute/folder` reads masks from another folder.

## Command-line options

| Option | Does |
| --- | --- |
| `-iwad /path/to/doom.wad` | Plays that game WAD. Use an absolute path. |
| `-file a.wad b.wad` | Loads custom WADs. |
| `-deh file.deh` | Applies extra DEHACKED patches. `-nodeh` ignores patches inside WADs. |
| `-blockmap` | Rebuilds each map's collision blockmap. |
| `-warp 1 1`, `-skill 4` | Starts a map directly. |
| `-width 1920 -height 1080` | Window size in window coordinates (Retina displays may give more pixels). 320×240 to 8192×8192. |
| `-fullscreen`, `-window` | Starts fullscreen or windowed. |
| `-fov 100` | Field of view. |
| `-classic` | Starts with the classic software renderer. |
| `-flashlight` | Starts with the flashlight on. `-flashlighttint 0`, `1` or `2` sets its tint. |
| `-fog 0`, `-palette 1` | Turns fog off, or the WAD-palette pass on. |
| `-classicmovement` | DOOM's own movement. |
| `-soundtrack pc` | `pc`, `psx`, `3do`, `idkfa` or `original` for this session. |
| `-soundfont bank.sf2`, `-opl`, `-systemsynth` | Picks the music device for this session (`-systemsynth` on the Mac only). |
| `-nosound`, `-nomusic` | Silences effects or music. |
| `-derpai` | Turns DERP mode's monster AI on in any mode, for testing. |

On the Mac, the program inside the app takes the same options:

```sh
"./Play DERP.app/Contents/MacOS/Play DERP" -iwad "/absolute/path/to/doom.wad"
```

The 3D renderer runs on Vulkan on Windows and Linux and on Metal on macOS.
Without a working driver the game starts with the classic renderer.
Multiplayer is not implemented.
