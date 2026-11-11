<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import type { CapturedMidiMessage } from '../midi/web-midi'
import { useMapperContext } from '../integration/context'
import { createControllerSimulator, inspectMidi } from '../midi/monitor'
const props = defineProps<{ template: ControllerTemplate; incoming?: CapturedMidiMessage; inputName?: string }>()
const engine = useMapperContext().engine
const mapping = computed(() => { try { return engine.resolve(JSON.parse(JSON.stringify(props.template))) } catch { return undefined } })
const expected = ref(''), control = ref(''), value = ref(64), error = ref(''), listening = ref(true)
type Row = ReturnType<typeof inspectMidi> & { source: string; timestamp: number; bytes: number[] }
const rows = ref<Row[]>([])
let simulator: ReturnType<typeof createControllerSimulator> | undefined
watch(mapping, m => { simulator = m ? createControllerSimulator(m) : undefined; rows.value = []; expected.value = ''; control.value = m?.assignments[0]?.control.id ?? '' }, { immediate: true })
function append(bytes: number[], source: string, timestamp: number, expectedId = expected.value) {
  if (!mapping.value || !bytes.length) return
  rows.value.unshift({ ...inspectMidi(bytes, mapping.value, expectedId || undefined), source, timestamp, bytes: [...bytes] })
  rows.value = rows.value.slice(0, 200)
}
watch(() => props.incoming, m => { if (m && listening.value) append(m.bytes, `${props.inputName || m.inputId} (${m.inputId})`, m.timestamp) })
function simulate(action: 'value' | 'press' | 'release') {
  error.value = ''
  try { append(simulator!.emit(control.value, action, value.value), 'Simulation', performance.now(), control.value) } catch (e) { error.value = (e as Error).message }
}
const selected = computed(() => mapping.value?.assignments.find(a => a.control.id === control.value))
const button = computed(() => selected.value && ['button', 'pad'].includes(selected.value.control.kind))
</script>
<template>
  <section class="monitor-panel" aria-label="MIDI monitor">
    <h2>MIDI monitor &amp; simulator</h2>
    <p>Select a MIDI input in Hardware connection to monitor it. Simulation runs locally and sends no MIDI. Latest 200 events; timestamps are browser milliseconds. Mapping edits clear the history and reset toggle state.</p>
    <p v-if="!mapping" role="alert">Resolve mapping errors to monitor or simulate.</p>
    <label>Expected physical control<select v-model="expected"><option value="">Any mapped address</option><option v-for="a in mapping?.assignments" :key="a.control.id" :value="a.control.id">{{ a.control.name }} · {{ a.parameter.name }} · {{ a.messageType }} {{ a.number }} ch {{ a.channel }} · {{ a.minimum }}–{{ a.maximum }}</option></select></label>
    <p>MIDI addresses can be shared. Select the control you are moving to compare its expected channel, number and range. A matching address alone cannot identify a physical control or verify a press/release sequence.</p>
    <div class="actions"><button @click="listening = !listening">{{ listening ? 'Pause monitor' : 'Resume monitor' }}</button><button @click="rows = []">Clear monitor</button></div>
    <label>Simulated control<select v-model="control"><option v-for="a in mapping?.assignments" :key="a.control.id" :value="a.control.id">{{ a.control.name }} · {{ a.parameter.name }}</option></select></label>
    <div v-if="selected" class="actions"><template v-if="button"><button @click="simulate('press')">Simulate press</button><button @click="simulate('release')">Simulate release</button></template><template v-else><label>Simulated value<input v-model.number="value" type="number" :min="selected.minimum" :max="selected.maximum"></label><button @click="simulate('value')">Simulate value</button></template></div>
    <p v-if="error" role="alert">{{ error }}</p>
    <p>Scroll the table horizontally to see every column. Keyboard: focus the table area, then use the arrow keys.</p>
    <div class="monitor-table" tabindex="0" role="region" aria-label="MIDI observations, horizontally scrollable"><table><thead><tr><th>Input</th><th>Time (ms)</th><th>Type</th><th>Channel</th><th>Number</th><th>Value</th><th>Elixir parameter(s)</th><th>Expected vs observed</th><th>Bytes</th></tr></thead><tbody><tr v-for="(r, i) in rows" :key="i"><td>{{ r.source }}</td><td>{{ r.timestamp.toFixed(1) }}</td><td>{{ r.type }}</td><td>{{ r.channel ?? '—' }}</td><td>{{ r.number ?? '—' }}</td><td>{{ r.value ?? '—' }}</td><td>{{ r.parameters.join(', ') || '—' }}</td><td>{{ r.status }} · {{ r.controls.join(', ') }}</td><td>{{ r.bytes.slice(0, 16).map(b => b.toString(16).padStart(2, '0')).join(' ') }}{{ r.bytes.length > 16 ? ' …' : '' }}</td></tr></tbody></table></div>
  </section>
</template>
