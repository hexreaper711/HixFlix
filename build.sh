#!/usr/bin/env bash
# Builds HixFlix for PS5 into dist/<titleId>/ and a zip of it: every step that isn't done yet, in
# the build container (dev.sh).
#   ./build.sh            the libraries (first run: a few hours on a Mac), libvlc, then the title
#   ./build.sh --deps     only the libraries and libvlc (no app code needed)
#   ./build.sh --title    only the title (the libraries must be there)
#   ./build.sh --host     the Linux test build (offscreen, scripted pad: host/run-tour.sh)
#   --version 1.0.5       with any of them: the version stamped in the app and the zip's name
#                         (dist/HixFlix-v1.0.5.zip; without it: VERSION.txt + "-dev", dist/<titleId>.zip)
# Needs Docker and ps5-plume-triangle with its built dependencies (see dev.sh).
set -euo pipefail

here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ ! -f /.dockerenv ]]; then
    exec "$here/dev.sh" bash /work/hixflix/build.sh "$@"
fi

deps=1 title=1 host=0 version=
while (( $# )); do
    case $1 in
    --deps) title=0 ;;
    --title) deps=0 ;;
    --host) deps=0 title=0 host=1 ;;
    --version) version=${2:?--version needs a value, e.g. 1.0.5}; version=${version#v}; shift ;;
    *) echo "unknown option $1 (see the top of build.sh)" >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n==> %s\n' "$*"; }
work=${VLC_PS5_WORK:?run through dev.sh}
vlc_version=3.0.24

if (( deps )); then
    if [[ ! -f $work/prefix-ps5/.hixflix-deps-done ]]; then
        step "VLC's libraries for the PS5 (FFmpeg, GnuTLS, libass, ...; once)"
        # one recipe at a time with a stamp each: a run that stopped goes on where it was
        mkdir -p "$work/stamps"
        for r in nasm compat zlib ffmpeg dav1d ebml matroska dvbpsi freetype fribidi harfbuzz gmp nettle \
                gnutls smb2 upnp libxml2 ass dvdread dvdnav bluray libarchive iconv; do
            [[ -f $work/stamps/$r ]] && continue
            TARGET=ps5 bash "$here/ps5/deps/build-deps.sh" "$r"
            touch "$work/stamps/$r"
        done
        touch "$work/prefix-ps5/.hixflix-deps-done"
    fi
    if [[ ! -d $work/src/vlc-$vlc_version ]]; then
        step "VLC $vlc_version source (once)"
        mkdir -p "$work/src" "$work/dl"
        [[ -f $work/dl/vlc-$vlc_version.tar.xz ]] ||
            curl -fsSL "https://download.videolan.org/pub/videolan/vlc/$vlc_version/vlc-$vlc_version.tar.xz" \
                -o "$work/dl/vlc-$vlc_version.tar.xz"
        tar xf "$work/dl/vlc-$vlc_version.tar.xz" -C "$work/src"
    fi
    if [[ ! -f $work/build-vlc-ps5/src/.libs/libvlccore.a ]]; then
        step "libvlc $vlc_version with every plugin linked in (once)"
        TARGET=ps5 bash "$here/ps5/configure-vlc.sh"
    fi
fi

if (( title )); then
    step "The HixFlix title${version:+ v$version}"
    HIXFLIX_VERSION=$version bash "$here/ps5/build-title.sh"
    title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$here/ps5/sce_sys/param.json")
    zip=${version:+HixFlix-v$version.zip}
    zip=${zip:-$title_id.zip}
    step "Packaging dist/$zip"
    (cd "$here/dist" && rm -f "$zip" && python3 -m zipfile -c "$zip" "$title_id")
    echo "dist/$zip: copy its $title_id folder to /data/homebrew on the PS5 (README.md)."
fi

if (( host )); then
    if [[ ! -f $work/prefix-host/.hixflix-deps-done ]]; then
        step "VLC's libraries for Linux (once)"
        mkdir -p "$work/stamps-host"
        for r in nasm zlib ffmpeg dav1d ebml matroska dvbpsi freetype fribidi harfbuzz gmp nettle \
                gnutls smb2 upnp libxml2 ass dvdread dvdnav bluray libarchive iconv; do
            [[ -f $work/stamps-host/$r ]] && continue
            TARGET=host bash "$here/ps5/deps/build-deps.sh" "$r"
            touch "$work/stamps-host/$r"
        done
        touch "$work/prefix-host/.hixflix-deps-done"
    fi
    if [[ ! -f $work/build-vlc-host/src/.libs/libvlccore.a ]]; then
        step "libvlc for Linux (once)"
        TARGET=host bash "$here/ps5/configure-vlc.sh"
    fi
    step "The Linux test build"
    bash "$here/host/build-app.sh"
fi
