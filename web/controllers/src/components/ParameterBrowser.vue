<script setup lang="ts">
import { computed, ref } from 'vue'
import { useMapperContext } from '../integration/context'
import { PARAMETER_DRAG_TYPE } from './led-colors'
import type { PhysicalControl } from '../core/mapping-engine/types'
const { parameters: registry } = useMapperContext()
const props = defineProps<{ control: PhysicalControl; channels?: Record<string, number> }>()
defineEmits<{ choose: [id: string] }>()
const query = ref(''), category = ref(''), scope = ref('all'), kind = ref('compatible')
const categories = [...new Set(registry.map(p => p.category))]
const parameters = computed(() => registry.filter(p => {
  const button = p.controlType === 'button' || p.messageType === 'program-change'
  const continuous = p.controlType === 'continuous' || (p.controlType === 'selection' && p.messageType === 'cc')
  const compatible = props.control.messageTypes.includes(p.messageType) && (['button', 'pad'].includes(props.control.kind) ? button : continuous)
  return (scope.value !== 'remote-tracks' || !p.track || p.track === 'd1') && (scope.value === 'all' || p.scope === scope.value) &&
    (kind.value === 'compatible' ? compatible : kind.value === 'continuous' ? continuous : kind.value === 'button' ? button : button || continuous) &&
    (!category.value || p.category === category.value) && `${p.name} ${p.id} ${p.category}`.toLowerCase().includes(query.value.trim().toLowerCase())
}))
function drag(event: DragEvent, id: string) {
  event.dataTransfer?.setData(PARAMETER_DRAG_TYPE, id)
  if (event.dataTransfer) event.dataTransfer.effectAllowed = 'copy'
}
</script>
<template>
  <div class="parameter-browser">
    <h3>Parameter browser</h3>
    <div class="parameter-modes" aria-label="Parameter scope"><button v-for="mode in [{ id: 'all', name: 'All' }, { id: 'remote-tracks', name: `Tracks · ch ${channels?.remoteTracks ?? 15}` }, { id: 'remote-global', name: `Global · ch ${channels?.remoteGlobal ?? 16}` }]" :key="mode.id" :aria-pressed="scope === mode.id" @click="scope = mode.id">{{ mode.name }}</button></div>
    <label>Search parameters<input v-model="query" type="search" placeholder="Filter, density, reverb…"></label>
    <label>Control type<select v-model="kind"><option value="compatible">Compatible with selected control</option><option value="continuous">◉ Continuous · knobs / sliders</option><option value="button">▦ Actions · buttons / pads</option><option value="all">All assignable types</option></select></label>
    <label>Category<select v-model="category"><option value="">All categories</option><option v-for="c in categories" :key="c">{{ c }}</option></select></label>
    <p>Drag onto a control, or select a parameter and press Apply assignment. Track families can fill a whole row.</p>
    <p>{{ parameters.length }} parameters</p>
    <div class="parameter-results"><button v-for="p in parameters" :key="p.id" draggable="true" @dragstart="drag($event, p.id)" @click="$emit('choose', p.id)">{{ scope === 'remote-tracks' && p.track === 'd1' ? p.name.replace(/^D1 /, '') : p.name }} <small><span class="parameter-type">{{ p.controlType === 'button' || p.messageType === 'program-change' ? '▦ Button / pad' : '◉ Knob / slider' }}</span> · {{ p.scope === 'remote-tracks' ? 'Tracks' : 'Global' }} · ch {{ p.channelGroup ? channels?.[p.channelGroup] ?? (p.channelGroup === 'remoteTracks' ? 15 : 16) : '—' }} · {{ p.messageType }} {{ p.number }}</small></button><p v-if="!parameters.length">No parameters match these filters.</p></div>
  </div>
</template>
