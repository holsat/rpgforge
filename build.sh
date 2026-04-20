#!/bin/bash
# Build script that excludes anaconda from PATH to avoid Qt/lib conflicts.
#
# Always runs cmake configure with Debug build type + RPGFORGE_DBUS_TESTING
# and RPGFORGE_DEBUG_LOGS explicitly enabled, so the resulting binary has
# the full diagnostic surface (RPGFORGE_DLOG macros, DBus test adaptor,
# full qDebug/qInfo output) regardless of any stale CMakeCache from a
# prior Release configure.
set -e

CLEAN_PATH=$(echo "$PATH" | tr ':' '\n' | grep -v anaconda | tr '\n' ':')
BUILD_DIR="$(dirname "$0")/build"

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# On clean, wipe the build directory first.
if [ "$1" = "clean" ]; then
    rm -rf ./*
fi

# Always re-configure with the debug flag set explicitly. cmake is
# incremental — if the cache already has these values, this is a no-op
# at build time. But if someone previously ran a Release build, or the
# cache is stale, this flips them back on without requiring a clean.
PATH="$CLEAN_PATH" cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DRPGFORGE_DBUS_TESTING=ON \
    -DRPGFORGE_DEBUG_LOGS=ON \
    -DCMAKE_IGNORE_PREFIX_PATH="/home/sheldonl/anaconda3" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

PATH="$CLEAN_PATH" make -j$(nproc)

echo ""
echo "Build complete: $BUILD_DIR/bin/rpgforge"
echo "  CMAKE_BUILD_TYPE=Debug"
echo "  RPGFORGE_DBUS_TESTING=ON"
echo "  RPGFORGE_DEBUG_LOGS=ON"
