#!/usr/bin/env bash
# Runs a command in HixFlix's Linux build container (x86-64 Ubuntu, Rosetta on Apple Silicon).
#   ./dev.sh                     an interactive shell
#   ./dev.sh bash build.sh       the whole PS5 build (see build.sh)
# This folder is /work/hixflix. ps5-plume-triangle (the PS5 payload SDK, RADV, the app CRT and
# the signer: what VLC-PS5's scripts call PS5_Vulkan) is mounted read-only at /work/vk, with its
# built dependencies from the Docker volumes the Splinter Cell port made. Its build folder (the host
# tool, and where RADV's link recipe writes its small files) is copied once into HixFlix's own
# volume (hixflix-plume-build), so nothing of the Splinter Cell port's is written to. The VLC
# libraries and build trees live on their own volume (hixflix-work) at /work/vlc-ps5: a Linux
# filesystem, faster than the shared Mac folder.
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
plume=${PLUME_DIR:-"$here/../splinter-cell-blacklist-ps5/thirdparty/ps5-plume-triangle"}
plume_deps=${PLUME_DEPS_VOLUME:-scbps5-plume-deps}
plume_build=${PLUME_BUILD_VOLUME:-scbps5-plume-build}
work=${HIXFLIX_WORK_VOLUME:-hixflix-work}
own_build=${HIXFLIX_PLUME_BUILD_VOLUME:-hixflix-plume-build}
image=${HIXFLIX_IMAGE:-scbps5-dev}

[[ -d $plume/tooling ]] || { echo "no ps5-plume-triangle at $plume (set PLUME_DIR)" >&2; exit 2; }
if ! docker image inspect "$image" >/dev/null 2>&1; then
    docker build --platform linux/amd64 -t "$image" "$here/docker"
fi

if ! docker run --rm --platform linux/amd64 -v "$own_build:/b" "$image" test -x /b/host/ps5-native-tool 2>/dev/null; then
    docker run --rm --platform linux/amd64 -v "$plume_build:/from:ro" -v "$own_build:/b" "$image" cp -a /from/. /b/
fi

tty=()
[[ -t 0 && -t 1 ]] && tty=(-it)
exec docker run --rm ${tty[@]+"${tty[@]}"} --platform linux/amd64 \
    -v "$here:/work/hixflix" \
    -v "$(cd "$plume" && pwd):/work/vk:ro" \
    -v "$plume_deps:/work/vk/.deps:ro" \
    -v "$own_build:/work/vk/build" \
    -v "$work:/work/vlc-ps5" \
    -e PS5_VULKAN=/work/vk -e VLC_PS5_WORK=/work/vlc-ps5 \
    -e PS5_PAYLOAD_SDK=/work/vk/.deps/native/ps5-payload-sdk \
    -e HIXFLIX_TMDB_KEY \
    -w /work/hixflix "$image" "${@:-bash}"
