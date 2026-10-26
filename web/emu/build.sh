#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir in the browser (after Felucca's web/emu/build.sh): the firmware's sources as web/emu/elixir_web.c builds them,
# compiled to WebAssembly with Emscripten, plus the page.
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, elixir.wasm, fonts/}
# Needs build/gen (tools/gen.sh: the tables, the fonts).
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/elixir_tables.h ] || { echo "emu: run tools/gen.sh first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten)"; exit 1; }
VERSION=$(sed -n 's/^#define ELIXIR_VERSION "\([^"]*\)".*/\1/p' firmware/src/elixir.c | head -n 1)
KITS=""
if [ -n "${ELIXIR_KITS_DIR:-}" ]; then          # 0.7: a private build with local LXR-02 kits (tools/build.py's notes)
    python3 tools/gen_factory_kits.py build/gen/factory_kits.h "$ELIXIR_KITS_DIR"
    KITS="-DELIXIR_FACTORY_KITS=1"
    VERSION="$VERSION+K"
fi
mkdir -p build/emu/fonts
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} $KITS \
    "-DELIXIR_VERSION=\"$VERSION\"" -Ibuild/gen -Ifirmware/src -Ifirmware/hal \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/elixir.wasm web/emu/elixir_web.c
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@//g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js web/library.js build/emu/
cp web/emu/fonts/DotGothic16-subset.woff web/emu/fonts/OFL.txt build/emu/fonts/
echo "emu: build/emu ($(wc -c < build/emu/elixir.wasm | tr -d ' ') B wasm, $VERSION)"
