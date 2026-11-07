# 8-channel USB audio: routing (Phase 8, 2026-11-06)

Deliverable 3 of the PRD.

## Voices and tracks

Six voices, seven tracks (voices.c:7, :20-22). Tracks 1-6 strike voices D1 D2 D3 SN CY HH; **track 7 (OH) strikes the
HH voice** with its open decay, so closed and open hats share one voice (and choke each other, as the LXR's). There
is no seventh audio source: track 7's hits are on the HH stem (render_test strikes each track alone and checks that
only its voice's stem changes).

## Signal flow and the taps

```
voice v: oscillators/models -> filter -> decimate -> post (drive, bits, tilt, out EG, VOLUME) -> INSERT
        -> cross modulation -> choke -> PUMP ---------------------------------> USB 3+v (stem, mono)
                                          |
                                          +-- OUT and recording: stop here
                                          v
                                   mix_voice: pan, FX SEND -> FX bus, REVERB SEND -> reverb, dry -> mix
                                          v
   mix -> SPACE dip -> FX bus + delay, reverb -> master COMPRESSOR -> master DIRT -> clip 16 bit
       -> DJ filter, TAPE STOP ---------------------------------------------------> USB 1-2 (the stereo mix)
                                          v
                                   x MASTER pot x master volume x hush -> DAC (the FM-1's outputs)
```

- **Stems** (voices.c:1119-1127): the block `mix_voice` receives, so the voice as the mixer hears it, with
  everything voice-local and the per-voice performance effects (choke, PUMP); before pan, sends and every bus. No
  global effect is ever applied to a stem, so none is applied twice when stems and mix are recorded together.
- **Mix** (engine.c:99-111): `ml`/`mr` after the DJ filter and TAPE STOP, i.e. exactly what the DAC is sent before the
  MASTER pot and the master volume. With the pot full and volume 127 it equals the FM-1's output sample for sample
  (render_test "USB 1-2 aren't the output" checks this). The pot doesn't change recordings.
- **Scale:** both 16-bit at the engine's own full scale (the LXR's DAC clip). A stem isn't attenuated (FNTSMG-1
  halves its tracks); a voice can't exceed full scale on its own.

## USB MIX: IN / OUT

CONFIG → USB MIX (two pages after MIDI OUT: D1 D2 D3 SN, then CY HH), values IN (default) / OUT.

- **OUT** skips the voice's `mix_voice` while the half is recorded: its dry sound, its FX SEND and its REVERB SEND all
  leave the mix (and so the FM-1's outputs, which play the mix). No echo or reverb tail of it remains in USB 1-2.
  Sends can't stay on independently: the bus and reverb are part of the mix; a voice's wet signal without its dry
  part is what FX SEND 127 already gives.
- **Its stem** plays on, sample for sample the same as with IN (render_test).
- **Only while a computer records** (`uac_block()` not NULL: the host has the stream open and is reading it).
  Unplugged or not recording, every voice is in the mix whatever USB MIX says. The page shows RECORDING / NOT
  RECORDING. So the toggle can't leave a voice silent on stage.
- Mute / solo, choke, velocity, morph, PERF and the sequencer are unchanged: a muted track's voice isn't struck, so its
  stem is silent too. The master compressor's sidechain voice (`mc_key`), if OUT, is left out like any other.
- **Persistence:** `kset.mixout` (project.c:964), the settings' former padding byte after `realign`: settings from
  before 1.0 hold 0 there (all IN); the struct's size is unchanged (280 B, asserted). Belongs to the device, not to
  kits or projects. ui_test checks the round trip and old settings.

## Channel map

| USB | Content |
|---|---|
| 1, 2 | the stereo mix L, R |
| 3 | D1 |
| 4 | D2 |
| 5 | D3 |
| 6 | SN |
| 7 | CY |
| 8 | HH (tracks CH and OH) |
