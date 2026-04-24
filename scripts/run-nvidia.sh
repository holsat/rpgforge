#!/usr/bin/env bash
# Launch RPG Forge on the Nvidia GPU via PRIME offloading.
#
# Why: the AMD/Mesa path through libgallium has segfaulted in Qt
# WebEngine's scene-graph compositor during preview rendering on this
# machine. Routing GL+Vulkan to the Nvidia card avoids the bad path.
# If/when the Mesa issue is fixed upstream, this script can be removed.
#
# Usage (from repo root):
#   ./scripts/run-nvidia.sh          # run already-built binary
#   ./scripts/run-nvidia.sh --rebuild # rebuild first
#
# Any additional arguments are forwarded to rpgforge itself.

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd)"
BIN="$REPO_ROOT/build/bin/rpgforge"

if [[ "${1:-}" == "--rebuild" ]]; then
    shift
    # Exclude anaconda from PATH so the KDE / Qt toolchain on $PATH is used.
    export PATH="$(printf '%s' "$PATH" | tr ':' '\n' | grep -v anaconda | paste -sd:)"
    cmake --build "$REPO_ROOT/build" -j"$(nproc)"
fi

if [[ ! -x "$BIN" ]]; then
    echo "rpgforge binary not found at $BIN" >&2
    echo "Build it first: cmake --build build -j\$(nproc)" >&2
    echo "or re-run this script with --rebuild" >&2
    exit 1
fi

# Nvidia PRIME offload env vars. Each targets a different graphics
# layer so WebEngine's Chromium compositor picks up Nvidia regardless
# of whether it goes through GLX, EGL, or Vulkan at runtime.
export __NV_PRIME_RENDER_OFFLOAD=1
export __GLX_VENDOR_LIBRARY_NAME=nvidia
export __VK_LAYER_NV_optimus=NVIDIA_only
# Also hint the Vulkan loader directly in case __VK_LAYER_NV_optimus
# is ignored on a given Mesa/Vulkan-loader combo.
if [[ -f /usr/share/vulkan/icd.d/nvidia_icd.json ]]; then
    export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/nvidia_icd.json
fi

echo "Launching rpgforge on Nvidia (PRIME offload)..."
exec "$BIN" "$@"
