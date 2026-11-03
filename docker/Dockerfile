# Build environment for HixFlix (the same image as the Splinter Cell: Blacklist PS5 port's):
# x86-64 Ubuntu 24.04 (runs under Rosetta on Apple Silicon). Covers the PS5 payload SDK +
# PS5_Mesa (RADV) in ps5-plume-triangle (clang 18) and VLC-PS5's libraries and title link.
FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential make bash ca-certificates git curl wget unzip tar xz-utils zstd rsync file \
        netcat-openbsd pkg-config cmake ninja-build ccache python3 python3-pip python3-mako \
        python3-ply python3-yaml glslang-tools spirv-tools bison flex \
        clang clang-format clang-tidy lld llvm \
        clang-19 lld-19 llvm-19 libclang-rt-19-dev \
        libx11-xcb-dev libwayland-dev libxkbcommon-dev libasound2-dev libudev-dev \
    && rm -rf /var/lib/apt/lists/*

# PS5_Mesa needs Meson 1.4 or newer; Ubuntu 24.04 has 1.3.2.
RUN python3 -m pip install --no-cache-dir --break-system-packages meson==1.6.1

# Keep build caches out of the (slower) bind-mounted source tree.
ENV CCACHE_DIR=/cache/ccache
WORKDIR /work
