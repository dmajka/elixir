#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the host tests a release must pass (.github/workflows/release.yml), after the build (its build/gen):
# the UI and features driven through the panel, and the bit-exact renders.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/shots
"$CC" -O1 -I build/gen -I firmware/src tests/ui_test.c -o build/ui_test -lm -w
build/ui_test build/shots > build/ui_test.log 2>&1 || { tail -40 build/ui_test.log; exit 1; }
tail -1 build/ui_test.log
"$CC" -O2 -I build/gen -I firmware/src tests/render_test.c -o build/render_test -lm -w
build/render_test build/ | tail -1
