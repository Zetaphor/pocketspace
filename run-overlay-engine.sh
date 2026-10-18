#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="${1:-$ROOT/scripts/avocado.js}"
export LD_LIBRARY_PATH="/opt/steamvr/bin/linuxarm64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DISPLAY="${DISPLAY:-:0}"
export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
cd "$ROOT"
exec "$ROOT/build/sf-overlay-engine" "$SCRIPT"
