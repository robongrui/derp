#!/bin/sh
# Packs dist/ from the Containerfile and the WAD folder into a Flatpak bundle and installs it for the current user.
# Run docker build -f Containerfile --output type=local,dest=dist . first.
# WADS_SRC points at a folder of IWADs (default /tmp/DOOM_wads, a clone of https://github.com/Akbar30Bill/DOOM_wads).
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP_ID=io.github.robongrui.DERP
WADS_SRC=${WADS_SRC:-/tmp/DOOM_wads}
OUT="$ROOT/build/flatpak"
APP="$OUT/app"
BIN="$APP/files/bin"

rm -rf "$OUT"
flatpak build-init "$APP" "$APP_ID" org.freedesktop.Sdk org.freedesktop.Platform 24.08

mkdir -p "$BIN/wads"
install -m755 "$ROOT/dist/play-derp" "$ROOT/dist/derp-heretic" "$BIN/"
cp -r "$ROOT/dist/soundfonts" "$BIN/"
for wad in doom1.wad doom.wad doomu.wad doom2.wad tnt.wad plutonia.wad heretic1.wad heretic.wad; do
    cp "$WADS_SRC/$wad" "$BIN/wads/"
done

install -Dm644 "$ROOT/flatpak/$APP_ID.desktop" "$APP/files/share/applications/$APP_ID.desktop"
install -Dm644 "$ROOT/platform/icon/derp.png" "$APP/files/share/icons/hicolor/256x256/apps/$APP_ID.png"

flatpak build-finish "$APP" \
    --command=play-derp \
    --share=ipc \
    --socket=x11 \
    --socket=wayland \
    --socket=pulseaudio \
    --device=dri

flatpak build-export "$OUT/repo" "$APP"
flatpak build-bundle "$OUT/repo" "$OUT/DERP.flatpak" "$APP_ID"
flatpak install --user --noninteractive --reinstall "$OUT/DERP.flatpak"
