#!/bin/bash
# Installiert die Raumklang-Plugins (AU für Logic/GarageBand, VST3) für den angemeldeten Benutzer.
cd "$(dirname "$0")"
set -e
AU="$HOME/Library/Audio/Plug-Ins/Components"
VST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$AU" "$VST"
for p in Plugins/*.component; do rm -rf "$AU/$(basename "$p")"; cp -R "$p" "$AU/"; done
for p in Plugins/*.vst3; do rm -rf "$VST/$(basename "$p")"; cp -R "$p" "$VST/"; done
# Die Builds sind nicht beglaubigt: Quarantäne entfernen, sonst lädt Logic sie nicht.
xattr -dr com.apple.quarantine "$AU"/Raumklang*.component "$VST"/Raumklang*.vst3 2>/dev/null || true
killall -9 AudioComponentRegistrar 2>/dev/null || true
echo
echo "Fertig. Die Plugins liegen in:"
echo "  $AU"
echo "  $VST"
echo "Logic bitte neu starten."
