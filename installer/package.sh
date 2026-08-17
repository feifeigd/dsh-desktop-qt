#!/usr/bin/env bash
# ============================================================
#  Merge Qt deploy (from build-win.bat) + embedded runtime,
#  then compile the NSIS installer.
#  Usage: bash package.sh
# ============================================================
set -euo pipefail

QT_DEPLOY=/mnt/c/dsh-desktop-qt/deploy
RUNTIME=/home/feifeigd/deploy-wsl-runtime
REPO=/home/feifeigd/dsh-desktop-qt
DIST="$REPO/../dist"

if [ ! -f "$QT_DEPLOY/DSHDesktop.exe" ]; then
  echo "ERROR: $QT_DEPLOY/DSHDesktop.exe missing — run build-win.bat first"
  exit 1
fi

echo "==> [1/3] Merging runtime into Qt deploy ..."
mkdir -p "$QT_DEPLOY/runtime" "$QT_DEPLOY/tsplugins" "$QT_DEPLOY/plugins"
cp -r "$RUNTIME/runtime/node"   "$QT_DEPLOY/runtime/node"
cp -r "$RUNTIME/runtime/dsh"    "$QT_DEPLOY/runtime/dsh"
cp -r "$RUNTIME/runtime/tools"  "$QT_DEPLOY/runtime/tools"
cp -r "$RUNTIME/tsplugins/dsh-desktop-demo" "$QT_DEPLOY/tsplugins/"

# The NSIS script reads from ..\deploy relative to installer/
echo "==> [2/3] Building installer with NSIS ..."
mkdir -p "$DIST"
NSIS="/mnt/c/Program Files (x86)/NSIS/makensis.exe"
cd "$REPO/installer"
"$NSIS" //NOCD //V2 dsh-desktop.nsi

echo "==> [3/3] Done:"
ls -lh "$DIST"
