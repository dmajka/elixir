/* SPDX-License-Identifier: GPL-3.0-only
 * Kettle host tests: the sound engine, the sequencer, kits and MIDI built for the host, without the UI, the
 * flash or the hardware (the stubs the firmware's other files would give). Include after <stdint.h>. */
#include <stdint.h>
#include <string.h>
#include "kettle_tables.h"
#include "lxr_tables.h"
#include "lxr_params.h"
#include "grids_map.h"
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define POOL                             /* (the firmware: .pool, the big zeroed RAM) */

/* usb.c's MIDI rings, as the firmware has them */
#define MQ 64u
static uint32_t midi_in_q[MQ];
static uint8_t midi_in_source[MQ];
static volatile uint32_t mi_w, mi_r;
static volatile uint8_t midi_in_overflow;
static uint32_t host_midi_out[4096];
static uint32_t host_midi_out_n;
static void midi_out_event(uint32_t pkt)
{
    if (host_midi_out_n < 4096u)
        host_midi_out[host_midi_out_n++] = pkt;
}
static int midi_enqueue(uint32_t pkt, uint32_t source)
{
    if (mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    midi_in_source[mi_w % MQ] = (uint8_t)source;
    mi_w++;
    return 1;
}

#include "../firmware/src/dsp.c"
#include "../firmware/src/voices.c"
#include "../firmware/src/fxbus.c"
#include "../firmware/src/reverb.c"
#include "../firmware/src/sound.c"
#include "../firmware/src/kit.c"

static uint32_t host_track, host_pattern;
static uint32_t ui_track(void) { return host_track; }
static uint32_t ui_pattern(void) { return host_pattern; }
static int ui_step_note_edit(uint32_t note) { (void)note; return 0; }

#include "../firmware/src/seq.c"
#include "../firmware/src/perform.c"
#include "../firmware/src/params.c"
#include "../firmware/src/midi.c"
#include "../firmware/src/engine.c"
