#!/usr/bin/env bash
# ============================================================
#  Prepare the embedded runtime for the Windows installer.
#  Run from WSL. Outputs to <repo>/../deploy-wsl-runtime
#  Layout produced:
#    deploy-wsl-runtime/runtime/node/node.exe + node_modules/npm
#    deploy-wsl-runtime/runtime/dsh/node_modules/@deepseek-ai/dsh
#    deploy-wsl-runtime/runtime/tools/{provision.js,update.js}
#    deploy-wsl-runtime/tsplugins/dsh-desktop-demo
#  The Qt exe + dlls are staged separately by build-win.bat into
#  C:\dsh-desktop-qt\deploy; merge the two before NSIS.
# ============================================================
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/../deploy-wsl-runtime"
NODE_VERSION="${NODE_VERSION:-v24.9.0}"
MIRROR="${MIRROR:-https://registry.npmmirror.com/-/binary/node}"

rm -rf "$OUT"
mkdir -p "$OUT/runtime/node" "$OUT/runtime/dsh" "$OUT/runtime/tools" "$OUT/tsplugins"

echo "==> [1/4] Downloading Windows node $NODE_VERSION ..."
NODE_ZIP="node-$NODE_VERSION-win-x64.zip"
curl -fsSL --retry 3 -o /tmp/$NODE_ZIP "$MIRROR/$NODE_VERSION/$NODE_ZIP"
echo "==> [2/4] Extracting node.exe + npm ..."
unzip -q -o /tmp/$NODE_ZIP -d /tmp/node-win-dist
cp /tmp/node-win-dist/node-$NODE_VERSION-win-x64/node.exe "$OUT/runtime/node/"
cp -r /tmp/node-win-dist/node-$NODE_VERSION-win-x64/node_modules/npm "$OUT/runtime/node/node_modules/"
# npm needs its bin shims (npm.cmd/npx.cmd) for some subcommands
cp /tmp/node-win-dist/node-$NODE_VERSION-win-x64/npm.cmd "$OUT/runtime/node/" 2>/dev/null || true
cp /tmp/node-win-dist/node-$NODE_VERSION-win-x64/npx.cmd "$OUT/runtime/node/" 2>/dev/null || true

echo "==> [3/4] Copying dsh harness (prebuilt deps incl. win32-x64) ..."
# The dsh dependency tree is assembled in WSL; native modules (node-pty,
# etc.) ship prebuilds for win32-x64, so the same tree runs on Windows.
cp -r "$REPO/../dsh-runtime-src/node_modules" "$OUT/runtime/dsh/node_modules"

echo "==> [4/4] Copying tools + tsplugins ..."
cp "$REPO/runtime/tools/provision.js" "$REPO/runtime/tools/update.js" "$OUT/runtime/tools/"
cp -r "$REPO/tsplugins/dsh-desktop-demo" "$OUT/tsplugins/"

echo "==> runtime size:"
du -sh "$OUT"
echo "DONE: $OUT"
