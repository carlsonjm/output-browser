#!/bin/bash
# Builds CEF with Widevine and the H.264/AAC media formats switched on, which
# CEF's published builds leave out. Needs about 100 GB free, git, python3 and curl; the
# compiler and a Debian sysroot are downloaded by Chromium's own tools, so it
# does not depend on Arch's packages. Takes several hours. No sudo.
# Usage: bash tools/build-cef.sh CHROMIUM-BRANCH   (the third part of a Chromium
# version: 8078 for 156.0.8078.109)
set -euo pipefail
W=${CEF_BUILD_DIR:-$HOME/cef-build}
mkdir -p "$W"; cd "$W"
BRANCH=${1:-}
[ -n "$BRANCH" ] || { echo "give a Chromium branch number, e.g. 8078"; exit 1; }
df -BG --output=avail "$W" | tail -1
curl -fsSLO https://raw.githubusercontent.com/chromiumembedded/cef/master/tools/automate/automate-git.py
export GN_DEFINES="is_official_build=true proprietary_codecs=true ffmpeg_branding=Chrome enable_widevine=true symbol_level=0 use_sysroot=true"
export CEF_ARCHIVE_FORMAT=tar.bz2
echo "branch $BRANCH, started $(date)" | tee build.log
python3 automate-git.py --download-dir="$W/download" --branch="$BRANCH" \
  --x64-build --no-debug-build --minimal-distrib --client-distrib --no-distrib-docs \
  --force-clean 2>&1 | tee -a build.log
echo "finished $(date)" | tee -a build.log
ls -lh "$W"/download/chromium/src/cef/binary_distrib/*.tar.bz2 | tee -a build.log
