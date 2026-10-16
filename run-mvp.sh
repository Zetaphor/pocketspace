#!/usr/bin/env bash
set -euo pipefail
ROOT="${SF_MVP_DIR:-$HOME/Code/sf-overlay-mvp}"
BIN="$ROOT/build/sf-mvp"
SCRIPT="${1:-$HOME/.sf-mvp/script.js}"
mkdir -p "$(dirname "$SCRIPT")"
if [ ! -f "$SCRIPT" ]; then
  printf 'let n=0;function update(dt){n++; if(n %% 1000==0) print("tick " + n);}\n' > "$SCRIPT"
fi
export LD_LIBRARY_PATH="/opt/steamvr/bin/linuxarm64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export DISPLAY="${DISPLAY:-:0}"
export EGL_PLATFORM="${EGL_PLATFORM:-x11}"
export SF_FONT="${SF_FONT:-/usr/share/fonts/TTF/Hack-Regular.ttf}"
exec "$BIN" "$SCRIPT"
