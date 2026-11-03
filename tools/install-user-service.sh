#!/usr/bin/env bash
# Installs the Pocketspace user services for this checkout. Usage:
#   tools/install-user-service.sh            install or update the units
#   tools/install-user-service.sh --remove   stop and remove them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# The user manager reads ~/.config/systemd/user, whatever this shell's XDG_CONFIG_HOME is.
UNITS="$HOME/.config/systemd/user"
if [[ "${1:-}" == "--remove" ]]; then
  systemctl --user disable --now pocketspace-autostart.service 2>/dev/null || true
  systemctl --user stop pocketspace.service 2>/dev/null || true
  rm -f "$UNITS/pocketspace.service" "$UNITS/pocketspace-autostart.service"
  systemctl --user daemon-reload
  exit 0
fi
mkdir -p "$UNITS"
for unit in pocketspace.service pocketspace-autostart.service; do
  sed "s|@ROOT@|$ROOT|g" "$ROOT/packaging/systemd/$unit" > "$UNITS/$unit.tmp"
  mv "$UNITS/$unit.tmp" "$UNITS/$unit"
done
systemctl --user daemon-reload
# Enabling is safe: the unit starts the engine only when engineAutostart is true.
systemctl --user enable pocketspace-autostart.service
echo "Installed. Start now with: systemctl --user start pocketspace.service"
