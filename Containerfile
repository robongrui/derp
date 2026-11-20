# Builds DERP for Linux (play-derp and derp-heretic) in a clean container.
# Extract the binaries with:
#   docker build -f Containerfile --output type=local,dest=dist .
# Ubuntu 24.04 (glibc 2.39) keeps the binaries runnable in the org.freedesktop.Platform 24.08 runtime (glibc 2.40).

FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config git ca-certificates curl \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
        libxtst-dev libxfixes-dev libxxf86vm-dev \
        libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev \
        libdbus-1-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev \
        libvulkan-dev libgl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build/play -DCMAKE_BUILD_TYPE=Release -DDOOM_BUILD_SDL=ON \
    && cmake --build build/play --parallel "$(nproc)"

FROM scratch AS artifact
COPY --from=build /src/build/play/play-derp /play-derp
COPY --from=build /src/build/play/derp-heretic /derp-heretic
COPY --from=build /src/build/play/soundfonts /soundfonts
COPY --from=build /src/build/play/play-derp.png /play-derp.png
