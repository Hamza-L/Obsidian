#!/bin/sh
# Obsidian build script (macOS): one clang call per executable, same options as the Makefile.
#
#   ./build.sh          build build/Obsidian (and the Metal shaders)
#   ./build.sh test     build and run build/Obsidian_test
#   ./build.sh clean    remove build/
#
# Options, set in the environment:
#   RENDERER_VALIDATION=1   graphics API validation (default 1)
#   ASAN=0                  AddressSanitizer (default 0)
#   V=0                     print every command (default 0)
set -eu
cd "$(dirname "$0")"

RENDERER_VALIDATION=${RENDERER_VALIDATION:-1}
ASAN=${ASAN:-0}
V=${V:-0}

APP=Obsidian
BUILD_DIR=build
SHADER_DIR=src/renderer/backend/metal/shaders

STB_IMAGE_COMMIT=013ac3beddff3dbffafd5177e7972067cd2b5083

CFLAGS="-std=c99 -g -DDEBUG -Wall -Wextra -Wpedantic -Werror -DRENDERER_VALIDATION=$RENDERER_VALIDATION"
CFLAGS="$CFLAGS -I./external -I./include -I./src"
LANGFLAGS="-x objective-c -fobjc-arc -fmodules"
LDFLAGS="-framework Cocoa -framework Metal -framework QuartzCore"

METALFLAGS="-std=metal4.0 -Wall -Werror -I./src"
if [ "$RENDERER_VALIDATION" = 1 ]; then
    METALFLAGS="$METALFLAGS -frecord-sources -gline-tables-only"
fi

if [ "$ASAN" = 1 ]; then
    CFLAGS="$CFLAGS -fsanitize=address -fno-omit-frame-pointer"
    LDFLAGS="$LDFLAGS -fsanitize=address"
fi

run() {
    if [ "$V" = 1 ]; then echo "$@"; fi
    "$@"
}

require() {
    if ! eval "$2" >/dev/null 2>&1; then
        echo "Missing $1. $3" >&2
        exit 1
    fi
}

deps_stb() {
    [ -f external/stb/stb_image.h ] && return
    echo "DEPS stb_image ($STB_IMAGE_COMMIT)"
    mkdir -p external/stb
    run curl -sSfL -o external/stb/stb_image.h "https://raw.githubusercontent.com/nothings/stb/$STB_IMAGE_COMMIT/stb_image.h"
}

build_shaders() {
    require "Metal toolchain" "xcrun -sdk macosx metal --version" "Install with: xcodebuild -downloadComponent MetalToolchain"
    mkdir -p "$BUILD_DIR/shaders"
    for src in "$SHADER_DIR"/*.metal; do
        [ -e "$src" ] || continue
        out="$BUILD_DIR/shaders/$(basename "$src" .metal).metallib"
        echo "METAL $src"
        run xcrun -sdk macosx metal $METALFLAGS "$src" -o "$out"
    done
}

# $1 = root source, $2 = output executable
build_exe() {
    mkdir -p "$BUILD_DIR"
    echo "BUILD $2"
    run clang $CFLAGS $LANGFLAGS "$1" -o "$2" $LDFLAGS
}

require "clang" "clang --version" "Install with: xcode-select --install"
require "curl" "curl --version" "curl ships with macOS."

case "${1:-app}" in
    app)
        deps_stb
        build_shaders
        build_exe src/main.c "$BUILD_DIR/$APP"
        ;;
    test)
        deps_stb
        build_exe test/test_main.c "$BUILD_DIR/${APP}_test"
        "./$BUILD_DIR/${APP}_test"
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        ;;
    *)
        echo "usage: $0 [app|test|clean]" >&2
        exit 1
        ;;
esac
