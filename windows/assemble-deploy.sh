#!/bin/bash
# Assembles deploy/ (final runtime layout) from a built exe. Shared by the CI
# workflow and local installer builds.
#   Usage: assemble-deploy.sh <path-to-built-rEFInd_GUI.exe> [deploy-dir]
set -euo pipefail

EXE="$1"
DEPLOY="${2:-deploy}"
BUILD_DIR="$(dirname "$EXE")"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

rm -rf "$DEPLOY"
mkdir -p "$DEPLOY"
# windeployqt can exit non-zero over a missing translations catalog on MSYS2;
# that is harmless here since copydeps.sh below completes the DLL closure.
windeployqt6 "$EXE" --dir "$DEPLOY" --no-translations --compiler-runtime >/dev/null 2>&1 || true
cp "$EXE" "$DEPLOY/"
# The privileged helper builds alongside the GUI and is the Scheduled
# Tasks' action (NATIVE_HELPER_DESIGN.md); it shares the GUI's DLL closure.
HELPER="$BUILD_DIR/rEFInd_GUI_helper.exe"
if [ ! -f "$HELPER" ]; then
    echo "Error: $HELPER not found next to the GUI exe. Aborting." >&2
    exit 1
fi
cp "$HELPER" "$DEPLOY/"
cp "$BUILD_DIR"/*.dll "$DEPLOY/" 2>/dev/null || true
for d in platforms styles imageformats iconengines tls networkinformation generic; do
    [ -d "$BUILD_DIR/$d" ] && cp -r "$BUILD_DIR/$d" "$DEPLOY/"
done
bash "$REPO/windows/copydeps.sh" "$DEPLOY" >/dev/null

mkdir -p "$DEPLOY/windows" "$DEPLOY/GUI"
cp "$REPO"/windows/*.ps1 "$DEPLOY/windows/"
cp -r "$REPO/icons" "$DEPLOY/"
cp -r "$REPO/backgrounds" "$DEPLOY/"
cp -r "$REPO/themes" "$DEPLOY/"
cp "$REPO/refind-GUI.conf" "$DEPLOY/GUI/refind.conf"

echo "deploy assembled at $DEPLOY"
echo "  exe:         $(ls -1 "$DEPLOY"/rEFInd_GUI.exe)"
echo "  helper:      $(ls -1 "$DEPLOY"/rEFInd_GUI_helper.exe)"
echo "  dll count:   $(ls -1 "$DEPLOY"/*.dll | wc -l)"
echo "  plugin dirs: $(cd "$DEPLOY" && ls -d platforms styles imageformats 2>/dev/null | tr '\n' ' ')"
echo "  ps1 scripts: $(ls -1 "$DEPLOY"/windows/*.ps1 | wc -l)"
echo "  seed conf:   $(ls "$DEPLOY"/GUI/refind.conf)"
