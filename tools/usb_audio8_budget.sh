#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir 1.0 (Phase 8): the 8-channel USB audio's resource budget (docs/usb-audio8-resource-budget.md).
#   tools/usb_audio8_budget.sh [BASE_REF]
# Builds the firmware with ELIXIR_USB_AUDIO=0 (MIDI only; the CONFIG page and its settings byte stay) and =8 (the
# mix and six stems, the default), and BASE_REF (a git ref from before the feature, e.g. be4c41e) in a scratch
# worktree if given; prints build.py's budget lines and the USB audio's symbols for each, and leaves the ELFs in
# build/usb8/. Ends with the default (8) build in build/, as ./build.sh leaves it. PYTHON as for ./build.sh.
set -e
cd "$(dirname "$0")/.."
OUT=build/usb8
mkdir -p "$OUT"
PY="${PYTHON:-python3}"
report() {   # $1 label, $2 log, $3 elf
    echo "== $1"
    grep -E "^  ok    (image|RAMTEXT|SRAM)" "$2" || true
    nm -S --size-sort "$3" 2>/dev/null | grep -E " (ua_|uac|ep4tx|mix_out|stems_live)" |
        while read -r a z t n; do printf "    %-18s %6d B  %s\n" "$n" "$((0x$z))" "$t"; done || true
}
if [ -n "$1" ]; then
    WT=$(mktemp -d "${TMPDIR:-/tmp}/elixir-base.XXXXXX")
    git worktree add -q --detach "$WT" "$1"
    (cd "$WT" && PYTHON="$PY" ./build.sh > build_base.log 2>&1) || { echo "base build failed: $WT/build_base.log"; exit 1; }
    cp "$WT/build_base.log" "$OUT/base.log"
    cp "$WT/build/elixir.elf" "$OUT/elixir_base.elf"
    git worktree remove --force "$WT"
    report "A: $1 (before the feature)" "$OUT/base.log" "$OUT/elixir_base.elf"
fi
for m in 0 8; do
    ELIXIR_USB_AUDIO=$m PYTHON="$PY" ./build.sh > "$OUT/build_$m.log" 2>&1 || { tail -20 "$OUT/build_$m.log"; exit 1; }
    cp build/elixir.elf "$OUT/elixir_$m.elf"
    cp build/elixir.bin "$OUT/elixir_$m.bin"
    report "ELIXIR_USB_AUDIO=$m" "$OUT/build_$m.log" "$OUT/elixir_$m.elf"
done
