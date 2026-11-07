# 8-channel USB audio: module design (Phase 8, 2026-11-06)

Deliverable 4 of the PRD. The transport is Felucca's usb.c with `FELUCCA_UAC_CH` (as felucca-bsp has it), so any
FM-1 app on Felucca's layer gets it by setting two macros and writing its channels.

## Layers

| Layer | Where | Knows about |
|---|---|---|
| USB transport: descriptors, EP4, packets, clock steering, stall / replug | usb.c (`uac_service`, `uac_packet`, `uac_stream`) | USB, the ring's read side |
| Sample format and ring | usb.c (`ua_ring[UA_N][UAC_NCH]` int16, `uac_render_start`, `uac_block`, `uac_block_done`) | frames, channels, blocks |
| Capture routing (which signal on which channel) | the app's adapter: Elixir's voices.c (stems), engine.c (mix) | the app's DSP graph |
| Main-mix inclusion | the app's mixer (Elixir: `mix_out` skips `mix_voice`) | the app's voices |

The USB module never mixes and can't take a voice out of a mix it is given: the app applies the mask before summing
(the PRD's ownership rule). No Elixir code is in usb.c.

## API (usb.c, `FELUCCA_UAC_CH` = N, 4..10 even)

```c
/* build: */
#define FELUCCA_UAC 1
#define FELUCCA_UAC_CH 8                 /* N channels; defines UAC_STEMS 1 and UAC_NCH N */
#define FELUCCA_UAC_RING_ATTR POOL       /* optional: where the 6 KB ring lives */
/* audio ISR, once a half (UAC_HALF frames, 128): */
void      uac_render_start(void);        /* decides whether this half is recorded (a host reading, room for it) */
/* for each BLOCK of the half: */
int16_t  *uac_block(void);               /* NULL: not recorded. Else BLOCK frames, frame i channel c at [i * N + c];
                                          * write every channel (0-1 the mix, 2..N-1 the app's) */
void      uac_block_done(void);          /* publish the block */
uint32_t  uac_recording(void);           /* this half is recorded (an app's "OUT only while recording") */
/* TIMER5, every 10 kHz tick, nested in the render too: */
void      uac_service(void);
uint32_t  uac_load_pm(uint32_t *glitches);   /* the service's share in 1/1000, glitches since power-on */
```

Rules: no heap; no checks needed by writers (the room is decided once a half); blocks never straddle the ring's end
(`UA_N`, `UA_PRIME` and `UAC_HALF` are multiples of `BLOCK`, asserted); one producer context (the audio ISR, core 0)
and one consumer (TIMER5, which the audio ISR can't interrupt). A second core must hand its channels to core 0 (Elixir:
core 1 renders D1-D3 into `vbuf`, core 0 copies the stems after the wait).

## Elixir's adapter (the whole of it)

```c
/* voices.c voices_block, after the cross-modulation/choke/PUMP of voice vi, block b: */
if (ub) {                                   /* ub = uac_block() once per block */
    int16_t *st = ub + 2u + vi;
    for (i = 0; i < BLOCK; i++, st += UAC_NCH) *st = b[i];
    if ((mix_out >> vi) & 1u) continue;     /* OUT: not mixed, no sends */
}
mix_voice(...);
/* engine.c engine_block, after the DJ filter: */
if ((ub = uac_block())) { for (i...) { ub[i*N] = ml[i]; ub[i*N+1] = mr[i]; } uac_block_done(); }
```

Another app with a different arrangement sets N (4..10) and writes its own channels; FM-1's 8-channel Elixir layout
is fixed by `ELIXIR_USB_AUDIO 8` (elixir.c), the host sees 8 channels whatever USB MIX says (no re-enumeration).

## Tests

- render_test "usb stems" (tests/engine_host.h supplies a flat-buffer `uac_block` so the real voices.c / engine.c
  write it): recording doesn't change the output; USB 1-2 equal the output bit for bit; each of the six stems sounds
  in the beat; D1 OUT leaves every stem bit-identical and takes D1 out of the mix; all OUT gives a silent mix (peak
  0: no sends left); OUT does nothing while not recording; each of the 7 tracks struck alone changes only its voice's
  stem (track 7 → HH).
- ui_test: USB MIX's pages and their order, labels (D1 mix … HH mix, IN / OUT), `kset.mixout` / `mix_out`, the
  settings' round trip and older settings.
- Not host-tested: usb.c's packet path (it needs the SIE). felucca-bsp's tests/usb_host_test.c tests its equivalent
  (descriptors for N channels, the packet's interleave); porting that harness is the next step if usb.c changes again.

## Future hardware

What a higher-RAM or high-speed FM-1 variant would change: a high-speed controller allows 24-bit or more channels
(full speed caps a 1 ms packet at 1,023 B); a different SIE needs fm1_usb.h's EP4 functions; a different I2S clock
needs `UA_FRAC44`; a different half or block needs `UAC_HALF` / `BLOCK` (asserted). Nothing is known of such
hardware; nothing here assumes it.
