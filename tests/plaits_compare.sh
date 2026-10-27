#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: builds and runs tests/plaits_compare.cc, plaits.c against Mutable Instruments Plaits' float code.
#   EURORACK=path/to/eurorack STMLIB=path/to/stmlib tests/plaits_compare.sh [ENGINE]
# (github.com/pichenettes/eurorack, its plaits/; github.com/pichenettes/stmlib.) Plaits' sample rate is set to
# Elixir's (44.1 kHz), and its string engine to one string (Elixir's), in a copy under build/plaits_ref.
set -e
cd "$(dirname "$0")/.."
: "${EURORACK:?set EURORACK to a checkout of pichenettes/eurorack}" "${STMLIB:?set STMLIB to pichenettes/stmlib}"
R=build/plaits_ref
rm -rf "$R" && mkdir -p "$R"
cp -R "$EURORACK/plaits" "$R/plaits"
cp -R "$STMLIB" "$R/stmlib"
sed -i.bak -e 's/kSampleRate = 48000.0f/kSampleRate = 44100.0f/' \
    -e 's/kCorrectedSampleRate = 47872.34f/kCorrectedSampleRate = 44100.0f/' "$R/plaits/dsp/dsp.h"
sed -i.bak 's/kNumStrings = 3/kNumStrings = 1/' "$R/plaits/dsp/engine/string_engine.h"
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal -c tests/plaits_core.c -o build/plaits_core.o
E="$R/plaits/dsp/engine"
c++ -O2 -std=c++11 -w -DTEST -I "$R" "$E"/*.cc "$R/plaits/dsp/chords/chord_bank.cc" \
    "$R/plaits/dsp/speech/"*.cc "$R/plaits/dsp/physical_modelling/"*.cc "$R/plaits/resources.cc" \
    "$R/stmlib/utils/random.cc" "$R/stmlib/dsp/units.cc" tests/plaits_compare.cc build/plaits_core.o \
    -o build/plaits_compare -lm
build/plaits_compare "$@"
