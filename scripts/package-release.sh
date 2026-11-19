#!/bin/sh
# Assemble a Windows or Linux release folder: the game, Freedoom as the default
# game, the GeneralUser GS SoundFont for the music, an empty wads folder for
# the player's own WADs, the docs and licenses.
# Usage: package-release.sh <windows|linux> <build dir> <Freedoom dir> <output dir>
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
platform=$1
build_dir=$2
freedoom_dir=$3
stage="$4/DERP-$platform"

program=
for candidate in "$build_dir/Release/Play DERP.exe" "$build_dir/Play DERP.exe" "$build_dir/play-derp"; do
    if [ -f "$candidate" ]; then
        program=$candidate
        break
    fi
done
if [ -z "$program" ]; then
    echo "Build the SDL app first; see BUILDING.md." >&2
    exit 1
fi

rm -rf "$stage"
mkdir -p "$stage/wads" "$stage/licenses/Freedoom"
cp "$program" "$stage/"
# The Heretic program beside it: the launcher hands Heretic WADs over.
heretic=
for candidate in "$(dirname -- "$program")/derp-heretic.exe" "$(dirname -- "$program")/derp-heretic"; do
    if [ -f "$candidate" ]; then
        heretic=$candidate
        break
    fi
done
if [ -z "$heretic" ]; then
    echo "Build derp-heretic too; see BUILDING.md." >&2
    exit 1
fi
cp "$heretic" "$stage/"
# The launcher, DERP: it finds the game WADs and starts the right program.
launcher=
for candidate in "$(dirname -- "$program")/DERP.exe" "$(dirname -- "$program")/derp"; do
    if [ -f "$candidate" ]; then
        launcher=$candidate
        break
    fi
done
if [ -z "$launcher" ]; then
    echo "Build derp-launcher too; see BUILDING.md." >&2
    exit 1
fi
cp "$launcher" "$stage/"
# Windows programs carry their icon; on Linux the window icon sits beside the game.
if [ "$platform" = linux ]; then
    cp "$(dirname -- "$program")/play-derp.png" "$stage/"
    # Files dropped on the launcher's icon (%F), and the WAD file types.
    cp "$project_dir/platform/launcher/derp.desktop" "$project_dir/platform/launcher/derp-wad.xml" "$stage/"
fi
cp "$freedoom_dir/freedoom1.wad" "$stage/default.wad"
# The cries without words (barks/derp-cries.wad), copied beside the game by the build.
if [ -f "$(dirname -- "$program")/derp-cries.wad" ]; then
    cp "$(dirname -- "$program")/derp-cries.wad" "$stage/"
fi
# Episode 0.5's map, generated at build time beside the game.
if [ -f "$(dirname -- "$program")/library.wad" ]; then
    cp "$(dirname -- "$program")/library.wad" "$stage/"
fi
cp "$project_dir/README.md" "$project_dir/PLAY.md" "$stage/"
cp "$project_dir/LICENSE.TXT" "$stage/licenses/GPL-3.txt"
cp "$project_dir/NOTICE.txt" "$stage/licenses/NOTICE.txt"
cp "$project_dir/THIRD_PARTY.md" "$stage/licenses/THIRD_PARTY.md"
cp "$project_dir/barks/VOICES-LICENSE.txt" "$stage/licenses/VOICES.txt"
cp "$build_dir/_deps/sdl3-src/LICENSE.txt" "$stage/licenses/SDL3.txt"
cp "$project_dir/third_party/SDL3-NOTICES.txt" "$stage/licenses/SDL3-third-party-notices.txt"
cp "$build_dir/_deps/nukedopl3-src/LICENSE" "$stage/licenses/Nuked-OPL3-LGPL-2.1.txt"
cp "$build_dir/_deps/tinysoundfont-src/LICENSE" "$stage/licenses/TinySoundFont-MIT.txt"
cp "$build_dir/_deps/drlibs-src/LICENSE" "$stage/licenses/dr_libs.txt"
cp "$build_dir/_deps/stb-src/LICENSE" "$stage/licenses/stb.txt"
mkdir -p "$stage/soundfonts"
cp "$build_dir/soundfonts/GeneralUser-GS.sf2" "$build_dir/soundfonts/GeneralUser-GS-LICENSE.txt" "$stage/soundfonts/"
for item in COPYING.txt CREDITS.txt CREDITS-MUSIC.txt README.html; do
    cp "$freedoom_dir/$item" "$stage/licenses/Freedoom/$item"
done
cat > "$stage/wads/PUT-WADS-HERE.txt" <<'EOF'
Put your game WADs in this folder, or anywhere: start DERP and drop them
on its window, or on its icon.

- Game WADs (doom.wad, doom2.wad, heretic.wad, freedoom1.wad, ...) here are
  found automatically, as are the ones Steam and GOG installed.
- Custom WADs are added in the launcher, in the order they load. A .deh or
  .bex patch with the same name next to a WAD is added with it.
- The included Freedoom game is always in the list.
EOF
echo "Ready: $stage"
