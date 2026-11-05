#!/usr/bin/env bash
# Installs the Pocketspace user services (engine, autostart and dashboard tab)
# for this checkout. Usage:
#   tools/install-user-service.sh            install or update the units
#   tools/install-user-service.sh --remove   stop and remove them
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# The user manager reads ~/.config/systemd/user, whatever this shell's XDG_CONFIG_HOME is.
UNITS="$HOME/.config/systemd/user"
if [[ "${1:-}" == "--remove" ]]; then
  systemctl --user disable --now pocketspace-autostart.service pocketspace-dashboard.service 2>/dev/null || true
  systemctl --user stop pocketspace.service 2>/dev/null || true
  rm -f "$UNITS/pocketspace.service" "$UNITS/pocketspace-autostart.service" "$UNITS/pocketspace-dashboard.service"
  systemctl --user daemon-reload
  exit 0
fi
mkdir -p "$UNITS"
for unit in pocketspace.service pocketspace-autostart.service pocketspace-dashboard.service; do
  sed "s|@ROOT@|$ROOT|g" "$ROOT/packaging/systemd/$unit" > "$UNITS/$unit.tmp"
  mv "$UNITS/$unit.tmp" "$UNITS/$unit"
done
systemctl --user daemon-reload
# Enabling is safe: the unit starts the engine only when engineAutostart is true.
systemctl --user enable pocketspace-autostart.service
# The dashboard tab starts with SteamVR from now on.
systemctl --user enable pocketspace-dashboard.service
echo "Installed. Start now with: systemctl --user start pocketspace.service"
echo "The dashboard tab starts with SteamVR; while SteamVR runs, start it with: systemctl --user restart pocketspace-dashboard.service"
