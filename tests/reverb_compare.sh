#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: builds and runs tests/reverb_compare.cc, reverb.c's PLATE against Mutable Instruments Clouds' reverb (float).
#   EURORACK=path/to/eurorack STMLIB=path/to/stmlib tests/reverb_compare.sh
# (github.com/pichenettes/eurorack, its clouds/dsp/fx/; github.com/pichenettes/stmlib.) In a copy under
# build/reverb_ref: the lengths scaled to Elixir's half rate (x 0.689), the LFOs' swings off, 32-bit float lines.
set -e
cd "$(dirname "$0")/.."
: "${EURORACK:?set EURORACK to a checkout of pichenettes/eurorack}" "${STMLIB:?set STMLIB to pichenettes/stmlib}"
R=build/reverb_ref
rm -rf "$R" && mkdir -p "$R/clouds/dsp/fx" "$R/stmlib/dsp"
cp "$EURORACK/clouds/dsp/fx/reverb.h" "$EURORACK/clouds/dsp/fx/fx_engine.h" "$R/clouds/dsp/fx/"
cp "$STMLIB/stmlib.h" "$R/stmlib/"
cp "$STMLIB/dsp/dsp.h" "$STMLIB/dsp/cosine_oscillator.h" "$R/stmlib/dsp/"
F="$R/clouds/dsp/fx/reverb.h"
sed -i.bak -e 's/Reserve<113,/Reserve<78,/' -e 's/Reserve<162,/Reserve<112,/' -e 's/Reserve<241,/Reserve<166,/' \
    -e 's/Reserve<399,/Reserve<275,/' -e 's/Reserve<1653,/Reserve<1139,/' -e 's/Reserve<2038,/Reserve<1404,/' \
    -e 's/Reserve<3411,/Reserve<2350,/' -e 's/Reserve<1913,/Reserve<1318,/' -e 's/Reserve<1663,/Reserve<1146,/' \
    -e 's/Reserve<4782>/Reserve<3295>/' \
    -e 's/c.Interpolate(ap1, 10.0f, LFO_1, 60.0f, 1.0f);/c.Interpolate(ap1, 7.0f, LFO_1, 0.0f, 1.0f);/' \
    -e 's/c.Write(ap1, 100, 0.0f);/c.Write(ap1, 69, 0.0f);/' \
    -e 's/c.Interpolate(del2, 4680.0f, LFO_2, 100.0f, krt);/c.Interpolate(del2, 3225.0f, LFO_2, 0.0f, krt);/' \
    -e 's/FxEngine<16384, FORMAT_12_BIT>/FxEngine<16384, FORMAT_32_BIT>/' "$F"
sed -i.bak 's/void Init(uint16_t\* buffer)/void Init(float* buffer)/' "$F"
cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal -c tests/reverb_core.c -o build/reverb_core.o
c++ -O2 -std=c++11 -w -DTEST -I "$R" tests/reverb_compare.cc build/reverb_core.o -o build/reverb_compare -lm
build/reverb_compare
