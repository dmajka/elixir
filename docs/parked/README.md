# Parked features

Code taken out of the firmware for flash, kept here so it can come back.

## 35i: PULSAR, WAVELET, SPLINTER (parked in 0.23-test5)

Three MICRO types after Curtis Roads' *Microsound*, built in 0.20 (Phase 35i), removed to make room for WANDER (about 2 KB of flash). A kit that used one now plays its LXR voice (kit_sanitize: the model is past the list).

To bring them back: `git apply docs/parked/35i-microsound.patch` from the repository root. It restores micro.c's three types, NMICRO 13 (rings.c), the names (params.c), the pictures and lines (ui_viz.c), render_test's names, and the manual and README text. The patch is against 0.23-test5; later edits to those files may need it applied by hand.
