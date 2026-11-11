<script setup lang="ts">
/** "Check it": what the controller sends, read back against the template, in Elixir's words. A slim stand-in for
 * the admin view's MIDI monitor (MidiMonitor.vue): it only listens to the input chosen above, and sends nothing. */
import { computed, ref, watch } from 'vue'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import type { CapturedMidiMessage } from '../midi/web-midi'
import { inspectMidi } from '../midi/monitor'
import { useMapperContext } from '../integration/context'
const props = defineProps<{ template: ControllerTemplate; incoming?: CapturedMidiMessage }>()
const { engine } = useMapperContext()
const mapping = computed(() => { try { return engine.resolve(JSON.parse(JSON.stringify(props.template))) } catch { return undefined } })
interface Line { key: number; ok: boolean; what: string; heard: string }
const lines = ref<Line[]>([])
let key = 0
// The guided part (as the XL 3's first write was checked): the first assigned control of each row, in order; each is
// ticked when its own message arrives, so a template that landed (channel, numbers, toggle) shows row by row.
const steps = computed(() => {
  const seen = new Set<string>()
  return (mapping.value?.assignments ?? []).filter(a => !seen.has(a.control.group) && seen.add(a.control.group))
    .map(a => ({ id: a.control.id, verb: a.control.kind === 'encoder' ? 'Turn' : a.control.kind === 'fader' ? 'Move' : 'Press', name: a.control.name, parameter: a.parameter.name }))
})
const done = ref<string[]>([])
const next = computed(() => steps.value.find(s => !done.value.includes(s.id)))
watch(mapping, () => { lines.value = []; done.value = [] })
watch(() => props.incoming, m => {
  if (!m || !mapping.value || m.bytes[0]! >= 0xf0) return            // (clock, SysEx: not a control)
  const r = inspectMidi(m.bytes, mapping.value)
  if (r.type === 'note' && r.value === 0) return                     // (a button's release)
  const heard = `${r.type === 'cc' ? 'CC' : r.type === 'note' ? 'Note' : r.type === 'program-change' ? 'Program' : r.type} ${r.number ?? ''} on channel ${r.channel}${r.value !== undefined ? ` = ${r.value}` : ''}`
  const control = mapping.value.assignments.find(a => r.controls.includes(a.control.id))
  const ok = r.status === 'address match' && !!control
  if (ok && steps.value.some(s => s.id === control!.control.id) && !done.value.includes(control!.control.id)) done.value = [...done.value, control!.control.id]
  const what = ok ? `${control!.control.name} → ${control!.parameter.name}` : r.parameters.length ? `Elixir: ${r.parameters.join(', ')} (not on this template)` : 'Not on this template, and not on Elixir’s remote map'
  lines.value = [{ key: key++, ok, what, heard }, ...lines.value].slice(0, 6)
})
</script>
<template>
  <section class="check" aria-label="Check the controller">
    <h3>Check it</h3>
    <ol v-if="steps.length" class="check-steps">
      <li v-for="s in steps" :key="s.id" :class="{ ok: done.includes(s.id), next: next?.id === s.id }"><span class="mark" aria-hidden="true">{{ done.includes(s.id) ? '✓' : '·' }}</span><span>{{ s.verb }} {{ s.name }}</span><small>{{ s.parameter }}</small></li>
    </ol>
    <p v-if="steps.length" class="check-empty" role="status">{{ next ? `Next: ${next.verb.toLowerCase()} ${next.name}.` : 'Every row sends what Elixir expects. Anything else you move shows below.' }}</p>
    <p v-if="!lines.length" class="check-empty">Move a knob or press a button on the controller: each message shows here with what it does on Elixir.</p>
    <ol v-else class="check-log" aria-live="polite">
      <li v-for="l in lines" :key="l.key" :class="{ ok: l.ok }"><span class="mark" aria-hidden="true">{{ l.ok ? '✓' : '?' }}</span><span>{{ l.what }}</span><small>{{ l.heard }}</small></li>
    </ol>
  </section>
</template>
<style scoped>
.check{margin-top:18px;padding-top:14px;border-top:1px solid var(--line,#2d2a25)}
.check h3{font-size:14px;margin:0 0 6px}
.check-empty{font-size:13px;color:var(--dim,#a29b8e);margin:0}
ol{list-style:none;margin:0;padding:0;display:grid;gap:4px}
li{display:grid;grid-template-columns:18px 1fr auto;gap:8px;align-items:baseline;font-size:13px;padding:5px 8px;border-radius:6px;background:var(--well,#201e1b)}
li small{color:var(--dim,#a29b8e);font:12px ui-monospace,SFMono-Regular,Menlo,monospace}
.mark{color:var(--warn,#ff9a5c);font-weight:700}
li.ok .mark{color:#4caf7a}
.check-steps{margin-bottom:8px}
.check-steps li{background:none;padding:2px 8px}
.check-steps li.next{background:var(--well,#201e1b);font-weight:600}
.check-steps .mark{color:var(--dim,#a29b8e)}
</style>
