#!/usr/bin/env bash
# Runs the Linux test build through a script of button presses and saves screenshots: HixFlix's
# screens without a console. lavapipe (Mesa's CPU Vulkan) draws them.
#   ./dev.sh bash host/run-tour.sh [script] [db.json]
# Defaults: host/demo/tour.txt, and the made-up library of host/demo/make-demo.py (pass "none"
# for an empty app, the first page). Shots land in host/demo/shots/.
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(dirname "$here")
work=${VLC_PS5_WORK:-$HOME/ps5/vlc-ps5}
script=${1:-$here/demo/tour.txt}
db=${2:-demo}
app=/tmp/hixflix-app

if ! ls /usr/share/vulkan/icd.d/lvp_icd*.json >/dev/null 2>&1; then
    apt-get update -qq >/dev/null && apt-get install -y -qq mesa-vulkan-drivers libvulkan1 >/dev/null
fi
rm -rf "$app"
mkdir -p "$app/fonts/fallback" "$app/certs" "$app/assets/lang" "$app/media"
cp "$repo"/assets/fonts/*.ttf "$app/fonts/"
cp "$repo"/assets/fonts/fallback/*.tt[fc] "$app/fonts/fallback/"
cp "$repo/assets/certs/cacert.pem" "$app/certs/"
cp "$repo/assets/vlc-logo.png" "$app/assets/"
cp "$repo"/assets/lang/*.txt "$app/assets/lang/"
if [[ $db == demo ]]; then
    python3 "$here/demo/make-demo.py" "$app" >/dev/null
elif [[ $db != none ]]; then
    cp "$db" "$app/hixflix-db.json"
fi
mkdir -p "$here/demo/shots"
cd "$here/demo"
VK_ICD_FILENAMES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json | head -1) VLCPS5_DATA=$app VLCPS5_SCRIPT=$script \
    timeout -s KILL "${TOUR_TIMEOUT:-300}" "$work/out/vlc-ps5-host" 2>&1 | grep -vE "^(\[toast\]|start:)" | tail -40 || true
ls "$here/demo/shots"
