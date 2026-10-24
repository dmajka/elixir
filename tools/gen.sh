#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Kettle: the generated headers (build/gen): the engine's fixed-point tables, the UI fonts, palettes and keycaps.
# PYTHON: a Python 3 with Pillow (and libraqm) and fontTools.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
[ -d /opt/homebrew/lib ] && export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"
mkdir -p build/gen
"$PY" tools/gen_tables.py build/gen/kettle_tables.h
"$PY" tools/gen_aa_font.py build/gen/ui_fonts.h --preset inter-tight 2>&1 | grep -v "Raqm\|warnings.warn" | tail -1
"$PY" tools/gen_ui_palettes.py build/gen/ui_palettes.h
"$PY" tools/gen_aa_keycaps.py build/gen/ui_keycaps.h 2>&1 | grep -v "Raqm\|warnings.warn" | tail -1
