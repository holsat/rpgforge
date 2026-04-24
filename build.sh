#!/bin/bash
# Build script that excludes anaconda from PATH to avoid Qt/lib conflicts.
#
# Usage:
#   ./build.sh                 # Debug build (default): full qDebug/qInfo
#                              # output + DBus test surface + RPGFORGE_DLOG
#   ./build.sh release         # Release build: -O2, logs silenced via
#                              # RPGFORGE_DEBUG_LOGS=OFF; use this when
#                              # profiling UI performance without log-
#                              # flush noise
#   ./build.sh clean           # Wipe build dir, then Debug build
#   ./build.sh clean release   # Wipe build dir, then Release build
#   ./build.sh release clean   # Same as above, arg order doesn't matter
set -e

CLEAN_PATH=$(echo "$PATH" | tr ':' '\n' | grep -v anaconda | tr '\n' ':')
BUILD_DIR="$(dirname "$0")/build"

CLEAN=0
MODE="debug"
for arg in "$@"; do
    case "$arg" in
        clean)   CLEAN=1 ;;
        release) MODE="release" ;;
        debug)   MODE="debug" ;;
        *)       echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ "$CLEAN" -eq 1 ]; then
    rm -rf ./*
fi

if [ "$MODE" = "release" ]; then
    BUILD_TYPE="Release"
    DEBUG_LOGS="OFF"
else
    BUILD_TYPE="Debug"
    DEBUG_LOGS="ON"
fi

# Always re-configure so toggling between debug and release flips the
# cache even without a clean. cmake is incremental; no-op if values
# match what's already in CMakeCache.
PATH="$CLEAN_PATH" cmake .. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DRPGFORGE_DBUS_TESTING=ON \
    -DRPGFORGE_DEBUG_LOGS="$DEBUG_LOGS" \
    -DCMAKE_IGNORE_PREFIX_PATH="/home/sheldonl/anaconda3" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

PATH="$CLEAN_PATH" make -j$(nproc)

echo ""
echo "Build complete: $BUILD_DIR/bin/rpgforge"
echo "  CMAKE_BUILD_TYPE=$BUILD_TYPE"
echo "  RPGFORGE_DBUS_TESTING=ON"
echo "  RPGFORGE_DEBUG_LOGS=$DEBUG_LOGS"
