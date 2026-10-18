#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
c++ -std=c++20 \
  -Iinclude -I"$HOME/.sdk/include" -I/usr/include/libdrm -I. \
  src/stereo_absolute_avocado_probe.cpp \
  -L/opt/steamvr/bin/linuxarm64 \
  -lopenvr_api -lpthread -lEGL -lGLESv2 -lgbm -ldrm \
  -o build/sf-stereo-absolute-avocado-probe
