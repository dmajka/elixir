#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir in the browser (after Felucca's web/emu/build.sh): the firmware's sources as web/emu/elixir_web.c builds them,
# compiled to WebAssembly with Emscripten, plus the page.
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, elixir.wasm, fonts/, kits/}
# Needs build/gen (tools/gen.sh: the tables, the fonts).
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/elixir_tables.h ] || { echo "emu: run tools/gen.sh first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define ELIXIR_VERSION "\([^"]*\)".*/\1/p' firmware/src/elixir.c | head -n 1)
python3 tools/gen_factory_kits.py build/gen/factory_kits.h assets/kits/factory >/dev/null   # (0.19: built in)
mkdir -p build/emu/fonts
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} \
    "-DELIXIR_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/elixir.wasm web/emu/elixir_web.c
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@//g" web/emu/index.html > build/emu/index.html
sed -e "s/@VERSION@/$VERSION/g" -e "s|@BASE@||g" -e "s|@SITE@|../|g" web/emu/mobile.html > build/emu/mobile.html   # (0.16: phones)
cp web/emu/worklet.js web/library.js build/emu/
python3 tools/gen_kit_packs.py build/emu/kits >/dev/null                # (0.19: the Hrtl kit packs, .ktlb)
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/elixir.wasm | tr -d ' ') B wasm, $VERSION)"
