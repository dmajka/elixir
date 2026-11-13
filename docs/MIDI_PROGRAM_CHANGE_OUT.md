# Program change out: following the sequencer's patterns on other gear

A design note from Elixir 1.4. It is written to stand on its own so another FM-1 app (Grimoire, the standalone MIDI
sequencer) can adopt the same behavior. Elixir's code is cited where it helps, but nothing here depends on it.

## The problem

The FM-1 sends MIDI only over USB (there is no TRS MIDI out). With a USB host in between (a computer, or a host box
such as a Kenton or iConnectivity), it can be the clock source for other gear, for example a Digitakt or Digitone.
When the FM-1 changes pattern, the other box should change its pattern too.

Elixir has always sent a program change on the global channel, using the pattern's own number, at the moment the new
pattern **started** (it inherited this from the LXR). That has three problems:

1. **It lands a pattern late on Elektron boxes.** A playing Elektron doesn't switch when it receives a program
   change. It queues the change and switches at the end of its own current pattern. If the program change arrives as
   Elixir's new pattern starts, the Elektron plays its old pattern one more time, so it runs a whole cycle behind.
2. **The numbers are fixed.** Elixir's pattern 2 always sent program 1 (the Digitakt's A02). A user may want
   Elixir's pattern 2 to call the Digitakt's A04, or not to touch the Digitakt at all.
3. **The channel is shared.** The program change went out on the global channel, which is also the channel Elixir
   launches its own patterns from. A user may want the other box on a different channel.

## The behavior

Three settings:

| Setting | Scope | Values | Default |
|---|---|---|---|
| **Sends** | per pattern, saved with the project | `AUTO` (the pattern's own index: pattern 1 sends 0) · a program `0–127` · `OFF` | `AUTO` |
| **Channel** | device setting | `GLOB` (follows the global channel) · `1–16` | `GLOB` |
| **When** | device setting | `AHEAD` · `AT CHANGE` | `AHEAD` |

The existing TX filter's **P** bit still switches all program change output off.

### When the program change goes out

The idea: at any moment there is one pattern the far end **should be heading for**. Whenever that pattern changes,
send its program change, once.

`AHEAD`:

| Situation | The pattern to announce |
|---|---|
| Playing, with a different pattern queued | the queued pattern, **as soon as it's queued** |
| Playing a song | the song's **next** pattern, as soon as the current row starts (for a row repeated ×N, during its last repeat) |
| Song mode, stopped or not yet started | the song's first row (PLAY starts there) |
| Otherwise (playing, nothing queued; or stopped) | the pattern playing |

`AT CHANGE`: always the pattern playing. This sends the program change as the new pattern starts, which is the old
behavior. It suits gear that switches immediately on receipt (for example an Elektron set to direct jump).

Stopped, a pattern change happens at once on both ends, so both modes send at once.

Re-queuing the same pattern sends nothing new, because the target didn't change. Queuing A and then B before the bar
ends sends A's program and then B's. An Elektron keeps the last one it received, so that's correct.

### Why "announce a target" rather than "send on events"

Elixir computes the target pattern in the **audio ISR, once per audio block**, compares it with the last one
announced, and sends only on a difference (`seq.c: seq_pc_out`). This has four advantages:

- **One context sends.** Patterns get queued from the UI (main loop), from incoming MIDI and from generative code. In
  Elixir the USB MIDI out ring is single-producer and fed from the ISR. Deriving the send from state means nothing
  outside the ISR has to touch the ring.
- **No duplicates.** Re-queues, reloads and echoes don't cause repeat sends, because the target didn't change.
- **The edge cases resolve themselves.** Song start, song loop, a manual queue during a song, and a project load
  that changes the playing pattern all come out right without special-case code.
- **It's cheap.** A few compares per block. Song look-ahead is computed once per pattern end (`seq_pattern_end`),
  not per block.

Song look-ahead uses the same "next row" function that advances the song, called without advancing. Where the next
row is chosen at random (Elixir's ROADS), that function picks once and caches the choice, so the look-ahead and the
real advance agree.

### Mapping the program

```
program(pattern) = AUTO → pattern index (0-based)
                   1..128 → stored value − 1  (program 0..127)
                   OFF → nothing is sent
```

Elixir stores this as one byte per pattern: `0 = AUTO`, `1..128 = program 0..127`, `129 = OFF`. With 0 as AUTO,
patterns from older projects (and blank patterns) behave exactly as before.

**Display**: `AUTO`, `OFF`, or the program number `0–127`. No device-specific names: an Elektron user knows that
A01–H16 are programs 0–127 (bank = program / 16, pattern = program % 16 + 1). The channel's default shows as
`GLOB 10` (the global channel's number), because `GLOBAL 10` doesn't fit on a card.

## Caveats to document for users

- **Matching lengths.** `AHEAD` makes both boxes switch at the end of their own patterns. They only switch on the
  same downbeat if those patterns are the same length (or one is a whole multiple of the other and the change falls
  on the longer one's end).
- **Feedback through the host.** If the other box also *sends* program changes (an Elektron's PRG CH SEND) on the
  channel the FM-1 *receives* pattern launches on, and the USB host routes them back, the FM-1 will follow them. With
  `AUTO` that's harmless: the box echoes the same pattern. With a remapped program it isn't. Elixir pattern 2 calls
  A04, the Digitakt echoes program 3, and Elixir queues its own pattern 4. Tell users to turn the other box's
  program change send off, or to use different channels. Elixir does not try to filter this in firmware.
- **USB not connected yet.** A program change sent before USB enumerates is dropped. The far end catches up at the
  next pattern change.

## Elixir's implementation (1.4)

| Piece | Where |
|---|---|
| Per-pattern byte `pcout` (`PCO_AUTO`, `PCO_OFF`) | `firmware/src/seq.c` (`pattern_t`) |
| Target logic, song look-ahead | `seq.c`: `seq_pc_out` (end of `seq_block_run`), `pc_next` in `seq_pattern_end` |
| Mapping and channel | `midi.c`: `midi_pc_prog`, `midi_pc_ch`, `midi_out_pc` |
| Saved | the project (version 14, in the pattern header); the settings' `pcch` and `pcwhen` (in the struct's former padding, so older and newer firmware read each other's settings) |
| UI | CONFIG → **PROGRAM CHANGE OUT**, after MIDI OUT: Pattern, Sends, Channel, When |
| Tests | `tests/ui_test.c: phase_140_pc` (stopped, AHEAD, remapped channel and program, OFF, AT CHANGE, song look-ahead with LOOP, project round trip, delete resets) |

Cost: one byte per pattern (plus the playing pattern's backup copy), two bytes of settings, and a few compares per
audio block. The project format grows only for patterns that set it.

### Notes for Grimoire

- If Grimoire has per-track MIDI channels and several devices, the per-pattern "Sends" could become a small list of
  (channel, program) pairs. The target-and-diff model carries over unchanged: announce each pair when the target
  pattern changes.
- Keep `AUTO` as the zero value, so blank and old data mean "send my own number".
- If Grimoire can queue patterns per track or per device (rather than one global pattern), compute one target per
  queue and diff each one separately.
