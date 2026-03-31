#!/bin/bash
# Build script that excludes anaconda from PATH to avoid Qt/lib conflicts
set -e

CLEAN_PATH=$(echo "$PATH" | tr ':' '\n' | grep -v anaconda | tr '\n' ':')
BUILD_DIR="$(dirname "$0")/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f CMakeCache.txt ] || [ "$1" = "clean" ]; then
    [ "$1" = "clean" ] && rm -rf ./*
    PATH="$CLEAN_PATH" cmake .. -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_IGNORE_PREFIX_PATH="/home/sheldonl/anaconda3" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

PATH="$CLEAN_PATH" make -j$(nproc)

echo ""
echo "Build complete: $BUILD_DIR/bin/rpgforge"
