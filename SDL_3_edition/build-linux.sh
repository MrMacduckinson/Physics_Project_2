#!/usr/bin/env bash
# Build a Linux binary using Ubuntu 22.04 (glibc) in Docker.
# SDL3, SDL3_ttf, and MuPDF are statically linked into the binary.
# glibc (libc.so.6, libstdc++.so.6) is linked dynamically — present on
# every Debian / Ubuntu / Devuan / Fedora / Arch / RPi OS system.
#
# Usage:
#   ./build-linux.sh                  # x86-64  → build-linux-amd64/
#   ./build-linux.sh arm64            # ARM64   → build-linux-arm64/
#   ./build-linux.sh arm64 --clean    # wipe build dir first
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARCH="${1:-amd64}"; [[ "$ARCH" == "--clean" ]] && ARCH="amd64"
BUILD_DIR="${SCRIPT_DIR}/build-linux-${ARCH}"

if [[ "${1:-}${2:-}" == *"--clean"* ]]; then
    echo "Removing old ${BUILD_DIR} ..."
    rm -rf "${BUILD_DIR}"
fi

if ! docker info &>/dev/null; then
    echo "Docker is not running. Start Docker Desktop and try again." >&2
    exit 1
fi

echo "==> Building physicsbooru for Linux/${ARCH} (Ubuntu 22.04 / glibc)..."

docker run --rm \
    --platform "linux/${ARCH}" \
    -v "${SCRIPT_DIR}:/src" \
    -e "BUILD_DIR=/src/build-linux-${ARCH}" \
    ubuntu:22.04 bash -c '
        set -xe
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -q --no-install-recommends \
            cmake make gcc g++ pkg-config \
            libx11-dev libxext-dev libxrandr-dev libxinerama-dev \
            libxcursor-dev libxi-dev libxtst-dev libxkbcommon-dev \
            libwayland-dev wayland-protocols libegl-dev \
            libasound2-dev

        rm -rf "$BUILD_DIR"
        mkdir  "$BUILD_DIR"
        cd     "$BUILD_DIR"

        cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DPHYSICSBOORU_STATIC=ON \
            -DSDL_X11_XSCRNSAVER=OFF \
            -DSDL_X11_XDBE=OFF

        make -j$(nproc)

        cp /src/assets/fonts/Roboto-Regular.ttf .
        echo ""
        echo "==> Done:"
        ls -lh physicsbooru Roboto-Regular.ttf
        echo "==> Runtime dependencies:"
        ldd physicsbooru | grep -v "linux-vdso\|ld-linux" || true
    '

echo ""
echo "Build complete → SDL_3_edition/build-linux-${ARCH}/"
echo "  physicsbooru        — copy this + the .ttf to the target Linux machine"
echo "  Roboto-Regular.ttf  — must be in the same directory as the binary"
