#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: builds and runs tests/rings_compare.cc, rings.c against Mutable Instruments Rings' float code.
#   EURORACK=path/to/eurorack STMLIB=path/to/stmlib tests/rings_compare.sh
# (github.com/pichenettes/eurorack, its rings/; github.com/pichenettes/stmlib.) Rings' sample rate is set to
# Elixir's (44.1 kHz), and its string's line to Elixir's length, in a copy under build/rings_ref.
set -e
cd "$(dirname "$0")/.."
: "${EURORACK:?set EURORACK to a checkout of pichenettes/eurorack}" "${STMLIB:?set STMLIB to pichenettes/stmlib}"
R=build/rings_ref
rm -rf "$R" && mkdir -p "$R"
cp -R "$EURORACK/rings" "$R/rings"
cp -R "$STMLIB" "$R/stmlib"
sed -i.bak 's/kSampleRate = 48000.0f/kSampleRate = 44100.0f/' "$R/rings/dsp/dsp.h"
sed -i.bak 's/kDelayLineSize = 2048/kDelayLineSize = 1024/' "$R/rings/dsp/string.h"   # Elixir's (RG_LINE)
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal -c tests/rings_core.c -o build/rings_core.o
c++ -O2 -std=c++11 -w -DTEST -I "$R" "$R/rings/dsp/resonator.cc" "$R/rings/dsp/string.cc" "$R/rings/dsp/fm_voice.cc" \
    "$R/rings/resources.cc" "$R/stmlib/utils/random.cc" "$R/stmlib/dsp/units.cc" tests/rings_compare.cc build/rings_core.o \
    -o build/rings_compare -lm
build/rings_compare
