#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the release build on GitHub's Ubuntu runner (.github/workflows/release.yml, The Firmware Hub's fm1-build):
# Pillow's text layout needs libraqm, which its wheels load with FriBiDi; then build.sh as a release.
#   tools/ci_build.sh X.Y        (writes build/elixir-X.Y.fwsc)
set -e
cd "$(dirname "$0")/.."
[ -n "$1" ] || { echo "ci_build: tools/ci_build.sh X.Y"; exit 1; }
PY="${PYTHON:-python3}"
if ! "$PY" -c 'from PIL import features; assert features.check("raqm")' 2>/dev/null; then
    SUDO=$(command -v sudo || true)
    $SUDO apt-get update -qq && $SUDO apt-get install -y -qq libfribidi0 >/dev/null
    "$PY" -c 'from PIL import features; assert features.check("raqm"), "Pillow has no raqm"'
fi
./build.sh --release "$1"
