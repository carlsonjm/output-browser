#!/usr/bin/env bash
# Sets up Output Browser on Firefox, for testing, without touching the
# system's Firefox or its profiles:
#   - a private copy of Firefox in <folder>/firefox, with our settings file
#     (config.js) that loads our frame code,
#   - its own profile in <folder>/profile,
#   - <folder>/output-browser-gecko.desktop for the app drawer.
#   bash gecko/install.sh [folder]        (default ~/Scratch/output-gecko)
# Run it again after changing the kit: it refreshes our files and keeps the
# profile's own data (sign-ins, extensions, history).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-$HOME/Scratch/output-gecko}
SRC=${FIREFOX_DIR:-/usr/lib/firefox}
[ -x "$SRC/firefox" ] || { echo "No Firefox at $SRC. Install firefox, or set FIREFOX_DIR."; exit 1; }
mkdir -p "$DEST"
if [ ! -x "$DEST/firefox/firefox" ] || [ "$SRC/omni.ja" -nt "$DEST/firefox/omni.ja" ]; then
  rm -rf "$DEST/firefox"
  cp -a "$SRC" "$DEST/firefox"
fi
mkdir -p "$DEST/firefox/defaults/pref"
cp "$HERE/autoconfig/config-prefs.js" "$DEST/firefox/defaults/pref/"
cp "$HERE/autoconfig/config.js" "$DEST/firefox/config.js"
mkdir -p "$DEST/firefox/distribution"
echo '{"policies":{"SearchEngines":{"Default":"DuckDuckGo"}}}' > "$DEST/firefox/distribution/policies.json"
mkdir -p "$DEST/profile/chrome"
cp "$HERE"/profile/chrome/* "$DEST/profile/chrome/"
sed "s|@START@|file://$DEST/profile/chrome/start.html|" "$HERE/profile/user.js" > "$DEST/profile/user.js"
cat > "$DEST/run.sh" <<RUN
#!/usr/bin/env bash
exec "$DEST/firefox/firefox" --name output-browser-gecko --class output-browser-gecko \\
  --profile "$DEST/profile" --no-remote "\$@"
RUN
chmod +x "$DEST/run.sh"
cat > "$DEST/output-browser-gecko.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=Output Browser (Firefox test)
Comment=Browse the web
Exec="$DEST/run.sh" %u
Icon=internet-web-browser
Terminal=false
Categories=Network;WebBrowser;
StartupNotify=true
StartupWMClass=output-browser-gecko
DESK
echo "Ready: $DEST/run.sh"
echo "To list it in the app drawer: cp \"$DEST/output-browser-gecko.desktop\" ~/.local/share/applications/"
