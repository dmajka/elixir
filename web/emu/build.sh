#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Kettle in the browser (after Felucca's web/emu/build.sh): the firmware's sources as web/emu/kettle_web.c builds them,
# compiled to WebAssembly with Emscripten, plus the page.
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, kettle.wasm, fonts/}
# Needs build/gen (tools/gen.sh: the tables, the fonts).
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/kettle_tables.h ] || { echo "emu: run tools/gen.sh first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define KETTLE_VERSION "\([^"]*\)".*/\1/p' firmware/src/kettle.c | head -n 1)
mkdir -p build/emu/fonts
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DKETTLE_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/kettle.wasm web/emu/kettle_web.c
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@//g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/kettle.wasm | tr -d ' ') B wasm, $VERSION)"
