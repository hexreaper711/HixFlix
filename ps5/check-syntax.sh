#!/usr/bin/env bash
# Compiles the app sources with the PS5 compiler, syntax only (no link): a quick check
# without the whole title build. Needs the libraries' headers (build.sh --deps).
#   ./dev.sh bash ps5/check-syntax.sh ui hixlib ...   (the names of files in src/, without .cc)
set -euo pipefail
w=/work/vlc-ps5
if [[ ! -d $w/src/vlc-3.0.24 ]]; then
  mkdir -p $w/dl $w/src
  [[ -f $w/dl/vlc-3.0.24.tar.xz ]] || curl -fsSL https://download.videolan.org/pub/videolan/vlc/3.0.24/vlc-3.0.24.tar.xz -o $w/dl/vlc-3.0.24.tar.xz
  tar xf $w/dl/vlc-3.0.24.tar.xz -C $w/src
fi
sdk=/work/vk/.deps/native/ps5-payload-sdk
r=/work/hixflix
cc() { PS5_PAYLOAD_SDK=$sdk sh /work/vk/tooling/prospero-clang18 "$@"; }
export PS5_CLANG=$(command -v clang || command -v clang-18)
flags=(-fsyntax-only -O2 -march=znver2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-result -Wno-unused-function -Wno-missing-field-initializers -DVK_NO_PROTOTYPES -DIMGUI_IMPL_VULKAN_USE_VOLK
  -I$r/src -I$r/third_party/imgui -I$r/third_party/volk -I$r/third_party/stb -I/work/vk/.deps/native/radv-release/include -I$w/src/vlc-3.0.24/include -I$w/prefix-ps5/include
  -std=c++17 -fno-exceptions -fno-rtti)
for f in "$@"; do echo "== $f"; cc "${flags[@]}" -c $r/src/$f.cc || true; done
