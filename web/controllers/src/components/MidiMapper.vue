<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, provide, ref, watch } from 'vue'
import HardwareConnection from './HardwareConnection.vue'
import ControllerPreview from './ControllerPreview.vue'
import SequencerPanel from './SequencerPanel.vue'
import { SEQUENCERS, sequencerById } from '../devices/sequencers'
import { LED_COLORS, PARAMETER_DRAG_TYPE } from './led-colors'
import { createMapperContext, mapperContextKey, type MidiMapperOptions } from '../integration/context'
import { ALL_PROFILES, canWrite, convertTemplate, FAMILIES, familyOf, modelName, profileById } from '../devices/families'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { LAUNCHPAD_PRO_MK3_FILE_SERIALIZER } from '../devices/novation/launchpad-pro-mk3/adapter'
import { LCXL_MK2_FILE_SERIALIZER } from '../devices/novation/launch-control-xl-mk2/adapter'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { generateTemplate, TEMPLATE_CATALOG, type BuiltinTemplateId } from '../elixir/templates'
import { generateLaunchpadTemplate, LAUNCHPAD_TEMPLATE_CATALOG, type LaunchpadTemplateId } from '../elixir/launchpad-templates'
import { DEFAULT_CHANNEL_SETTINGS, validateChannelSettings } from '../elixir/channel-settings'
import { createEmptyTemplate } from '../elixir/mapping-context'
import { REGISTRY_SOURCE } from '../elixir/parameter-registry'
import { assignTrackRow } from '../elixir/assignment-tools'
import { exportTemplate, importTemplate } from '../core/mapping-engine/schema'
import type { ControllerTemplate } from '../core/mapping-engine/types'
const props = defineProps<{ options?: MidiMapperOptions }>()
const emit = defineEmits<{ 'template-change': [template: ControllerTemplate]; 'save-template': [template: ControllerTemplate] }>()
const options = JSON.parse(JSON.stringify(props.options ?? {})) as MidiMapperOptions
const context = createMapperContext(options.parameterOverrides)
provide(mapperContextKey, context)
const { engine, parameters } = context
const devices = ALL_PROFILES.filter(p => !options.availableDevices || options.availableDevices.includes(p.id))
if (!devices.length) throw new Error('Enable at least one supported mapper device')
// Families (Launch Control XL, Launchpad) in the toolbar, and the model within one: a template moves between models
const families = FAMILIES.map(f => ({ ...f, models: f.models.filter(m => devices.includes(m)) })).filter(f => f.models.length)
const isLaunchpad = (id: string) => familyOf(id)?.id === 'launchpad'
const messageTypeOf = (id: string) => parameters.find(p => p.id === id)?.messageType
/** a template made for the family's first model (the XL 3's presets), on the model `id` */
function onModel(t: ControllerTemplate, id: string) { return t.deviceProfileId === id ? { template: t, dropped: [] } : convertTemplate(t, profileById(id)!, messageTypeOf) }
const droppedNote = (dropped: string[], model: string) => dropped.length ? ` ${dropped.length} control${dropped.length > 1 ? 's' : ''} left empty: the ${model} can't send what ${dropped.length > 1 ? 'they' : 'it'} had.` : ''
const channels = options.defaultChannels ?? DEFAULT_CHANNEL_SETTINGS
validateChannelSettings(channels)
const deviceId = ref(devices[0]!.id)
const profile = computed(() => devices.find(p => p.id === deviceId.value)!)
const family = computed(() => families.find(f => f.models.some(m => m.id === deviceId.value))!)
const lastModel: Record<string, string> = {}
watch(deviceId, id => { lastModel[familyOf(id)!.id] = id }, { immediate: true })
const SERIALIZERS = [XL3_FILE_SERIALIZER, LCXL_MK2_FILE_SERIALIZER, LAUNCHPAD_PRO_MK3_FILE_SERIALIZER]
const serializer = computed(() => SERIALIZERS.find(s => s.deviceProfileId === deviceId.value))
const writable = computed(() => canWrite(profile.value)), exportable = computed(() => !!serializer.value)
function initialTemplate(id: string): ControllerTemplate {
  const next = isLaunchpad(id) ? generateLaunchpadTemplate('pad-mute-solo', channels) : generateTemplate('mixer', channels)
  // Track layouts use seven explicit tracks; selected-track CCs are not master controls.
  next.assignments = next.assignments.filter(a => !a.parameterId.startsWith('tracks.selected.'))
  if (!isLaunchpad(id)) next.assignments.push({ controlId: 'fader.8', parameterId: 'tracks.master-volume' })
  return onModel(next, id).template
}
const template = ref(initialTemplate(deviceId.value)), selected = ref(profile.value.controls[0]!.id)
const tab = ref<'tracks' | 'global' | 'control' | 'setup'>('tracks'), category = ref(''), picked = ref(''), error = ref(''), status = ref('')
const saved = ref((options.savedTemplates ?? []).map(t => { const next = importTemplate(exportTemplate(t)); ensureAvailable(next); engine.resolve(next); return next }))
const preset = ref('current'), fillTracks = ref(true)
const control = computed(() => profile.value.controls.find(c => c.id === selected.value)!)
// The controls selected together (Shift-click, Cmd/Ctrl-click, a drag: ControllerPreview); `selected` is the one in
// focus, always among them. Config's colors and Clear apply to all of them.
const selection = ref<string[]>([selected.value])
watch(selected, id => { if (!selection.value.includes(id)) selection.value = [id] })
const targets = computed(() => profile.value.controls.filter(c => selection.value.includes(c.id)))
const multi = computed(() => targets.value.length > 1)
const assignedTargets = computed(() => targets.value.filter(c => template.value.assignments.some(a => a.controlId === c.id)))
const colorStates = computed(() => [{ active: false, name: 'LED color' }, { active: true, name: 'Active color' }]
  .map(state => {
    const able = targets.value.filter(c => state.active ? c.led.on : c.led.off)
    const colors = [...new Set(able.flatMap(c => c.led.colors))]
    const values = template.value.assignments.filter(a => able.some(c => c.id === a.controlId)).map(a => state.active ? a.colors?.on : a.colors?.off)
    return { ...state, enabled: able.length > 0, colors, current: values.length && values.every(v => v === values[0]) ? values[0] : undefined }
  }))
const targetSummary = computed(() => {
  const kinds = [...new Set(targets.value.map(c => ({ encoder: 'knobs', fader: 'faders', button: 'buttons', pad: 'pads' } as Record<string, string>)[c.kind] ?? c.kind))]
  const unassigned = targets.value.length - assignedTargets.value.length
  return `${kinds.join(', ')}${unassigned ? ` · ${unassigned} unassigned (colors need a parameter)` : ''}`
})
function selectMany(ids: string[], focus: string) { selection.value = ids; selected.value = ids.includes(focus) ? focus : ids.at(-1)! }
function selectOnly() { selection.value = [selected.value] }
const assignment = computed(() => template.value.assignments.find(a => a.controlId === selected.value))
const catalog = computed(() => isLaunchpad(deviceId.value) ? LAUNCHPAD_TEMPLATE_CATALOG : TEMPLATE_CATALOG)
const report = computed(() => engine.validate(snapshot()))
function snapshot() { return JSON.parse(JSON.stringify(template.value)) as ControllerTemplate }
function ensureAvailable(t: ControllerTemplate) { if (!devices.some(p => p.id === t.deviceProfileId)) throw new Error('Template device is unavailable') }
const scopeParameters = computed(() => parameters.filter(p => p.scope === (tab.value === 'tracks' ? 'remote-tracks' : 'remote-global') && (!p.track || p.track === 'd1') && p.controlType !== 'realtime' && [p.messageType, ...p.alternateMessageTypes].some(t => emits.value.has(t)) && (hasContinuous.value || p.controlType === 'button' || p.messageType === 'program-change')))
// what this model's controls can send: no knobs on a Launchpad, no program changes from an XL mk2
const emits = computed(() => new Set<string>(profile.value.controls.flatMap(c => c.messageTypes)))
const hasContinuous = computed(() => profile.value.controls.some(c => c.kind === 'encoder' || c.kind === 'fader'))
const sheet = computed(() => profile.value.controls.flatMap(c => {
  const a = template.value.assignments.find(a => a.controlId === c.id), p = a && parameters.find(p => p.id === a.parameterId)
  if (!a || !p) return []
  const type = a.messageType ?? p.messageType
  return [{ id: c.id, control: c.name, name: p.name, message: type === 'program-change' ? `Program ${p.number}` : `${type === 'cc' ? 'CC' : 'Note'} ${p.number}`, channel: a.channelOverride ?? (p.channelGroup ? template.value.channels[p.channelGroup] : undefined), mode: c.kind === 'button' || c.kind === 'pad' ? (a.buttonMode ?? 'momentary') : '' }]
}))
const selectedParameterName = computed(() => parameters.find(p => p.id === assignment.value?.parameterId)?.name ?? 'Unassigned')
const tabsElement = ref<HTMLElement>()
const TABS = ['tracks', 'global', 'control', 'setup'] as const
const TAB_NAMES = { tracks: 'Tracks', global: 'Global', control: 'Control', setup: 'Setup' }
const selectedParameter = computed(() => parameters.find(p => p.id === assignment.value?.parameterId))
const remoteChannelOf = (p: { channelGroup?: string }) => p.channelGroup === 'remoteTracks' ? template.value.channels.remoteTracks : template.value.channels.remoteGlobal
const kindOf = (p: { messageType: string; number: number }) => p.messageType === 'program-change' ? `program change ${p.number}` : `${p.messageType === 'cc' ? 'CC' : p.messageType} ${p.number}`
const isMac = typeof navigator !== 'undefined' && /Mac|iPhone|iPad/.test(navigator.platform || navigator.userAgent)
async function stepTab(direction: number) {
  const names = TABS
  tab.value = names[(names.indexOf(tab.value) + direction + names.length) % names.length]!
  await nextTick(); tabsElement.value?.querySelector<HTMLElement>('[aria-selected=true]')?.focus()
}
const categories = computed(() => [...new Set(scopeParameters.value.map(p => p.category))])
const visibleParameters = computed(() => scopeParameters.value.filter(p => !category.value || p.category === category.value))
const continuous = computed(() => visibleParameters.value.filter(p => p.controlType === 'continuous' || p.controlType === 'selection' && p.messageType === 'cc'))
const actions = computed(() => visibleParameters.value.filter(p => p.controlType === 'button' || p.messageType === 'program-change'))
const parameterGroups = computed(() => [
  ...(hasContinuous.value ? [{ name: 'Knobs & sliders', symbol: '◉', items: continuous.value }] : []),
  { name: 'Buttons & pads', symbol: '▦', items: actions.value },
])
watch(tab, () => { category.value = ''; picked.value = '' })
watch(template, () => emit('template-change', snapshot()), { deep: true })
/** Another family starts from its own first template; another model of the same family keeps your edits. */
function switchDevice(id: string) {
  if (id === deviceId.value || !devices.some(p => p.id === id)) return
  const sameFamily = familyOf(id) === familyOf(deviceId.value)
  const { template: next, dropped } = sameFamily ? convertTemplate(snapshot(), profileById(id)!, messageTypeOf) : { template: initialTemplate(id), dropped: [] }
  next.channels = { ...template.value.channels }
  deviceId.value = id; template.value = next
  if (!profile.value.controls.some(c => c.id === selected.value)) selected.value = profile.value.controls[0]!.id
  selection.value = [selected.value]
  picked.value = ''; preset.value = 'current'; error.value = ''
  status.value = sameFamily ? `Now for the ${profile.value.model}.${droppedNote(dropped, profile.value.model)}` : 'Controller ready.'
}
/** the model the dialog found on the MIDI ports (HardwareConnection), when it isn't the one drawn */
function useModel(id: string) { const was = deviceId.value; switchDevice(id); if (deviceId.value !== was) status.value = `Found on the MIDI ports. ${status.value}` }
// Sequencers (Hapax, OXI One): files to download from the site's data/, no template; #hapax, #oxi open one. Only on
// the site (a parameter JSON URL), where web/make_site.py writes them.
const dataUrl = options.parameterJsonUrl?.replace(/[^/]*$/, '')
const sequencer = ref(dataUrl ? sequencerById(location.hash.slice(1))?.id ?? '' : '')
const sequencerShown = computed(() => sequencerById(sequencer.value))
watch(sequencer, s => history.replaceState(null, '', s ? `#${s}` : location.pathname + location.search))
/** the narrow toolbar's one list: every controller model, then the sequencers */
const deviceChoice = computed(() => sequencer.value ? `seq:${sequencer.value}` : deviceId.value)
function chooseDevice(v: string) { if (v.startsWith('seq:')) sequencer.value = v.slice(4); else { sequencer.value = ''; switchDevice(v) } }
const switchFamily = (id: string) => { sequencer.value = ''; const f = families.find(f => f.id === id)!; switchDevice(f.models.find(m => m.id === lastModel[id])?.id ?? f.models[0]!.id) }
function select(id: string) { selection.value = [id]; selected.value = id; if (picked.value) assign(id, picked.value) }
function assign(controlId: string, parameterId: string) {
  try {
    const target = profile.value.controls.find(c => c.id === controlId)
    let next = snapshot()
    if (fillTracks.value && target?.column === 0 && /^tracks\.(d1|d2|d3|sn|cy|ch|oh|selected)\./.test(parameterId)) next = assignTrackRow(next, profile.value, parameters, controlId, parameterId)
    else {
      next.assignments = next.assignments.filter(a => a.controlId !== controlId)
      next.assignments.push({ controlId, parameterId })
    }
    engine.resolve(next); template.value = next; selected.value = controlId; error.value = ''
    status.value = `${parameters.find(p => p.id === parameterId)?.name ?? parameterId} assigned.`
    picked.value = ''
  } catch (e) { error.value = (e as Error).message }
}
function drag(event: DragEvent, id: string) { event.dataTransfer?.setData(PARAMETER_DRAG_TYPE, id); if (event.dataTransfer) event.dataTransfer.effectAllowed = 'copy' }
function setColor(color: number, active = false) {
  const next = snapshot()
  const able = targets.value.filter(c => (active ? c.led.on : c.led.off) && c.led.colors.includes(color))
  const changed = next.assignments.filter(a => able.some(c => c.id === a.controlId))
  if (!changed.length) { error.value = multi.value ? 'Assign parameters to these controls first.' : 'Assign a parameter to this control first.'; return }
  changed.forEach(a => { a.colors = { ...a.colors, [active ? 'on' : 'off']: color } })
  const skipped = targets.value.length - changed.length
  try {
    engine.resolve(next); template.value = next; error.value = ''
    status.value = multi.value ? `Color set on ${changed.length} controls${skipped ? `; ${skipped} skipped (unassigned or no such color)` : ''}.` : 'Color updated.'
  } catch (e) { error.value = (e as Error).message }
}
function unassign() {
  const ids = new Set(selection.value)
  template.value.assignments = template.value.assignments.filter(a => !ids.has(a.controlId)); error.value = ''
  status.value = multi.value ? `${ids.size} controls cleared.` : 'Control cleared.'
}
function loadPreset() {
  try {
    if (preset.value === 'current') return
    const ch = { remoteTracks: template.value.channels.remoteTracks!, remoteGlobal: template.value.channels.remoteGlobal! }
    let next: ControllerTemplate
    if (preset.value === 'blank') next = { ...createEmptyTemplate('user.new', 'Elixir Custom', deviceId.value), channels: ch }
    else if (preset.value.startsWith('saved:')) next = importTemplate(exportTemplate(saved.value[Number(preset.value.slice(6))]!))
    else {
      const made = isLaunchpad(deviceId.value) ? { template: generateLaunchpadTemplate(preset.value as LaunchpadTemplateId, ch), dropped: [] } : onModel(generateTemplate(preset.value as BuiltinTemplateId, ch), deviceId.value)
      next = made.template; status.value = droppedNote(made.dropped, profile.value.model).trim()
    }
    ensureAvailable(next); engine.resolve(next)
    template.value = next; deviceId.value = next.deviceProfileId; selected.value = profile.value.controls[0]!.id; error.value = ''
  } catch (e) { error.value = (e as Error).message }
}
function download(kind: 'syx' | 'json') {
  try {
    const next = snapshot()
    if (kind === 'syx' && !serializer.value) throw new Error(`No device file for the ${profile.value.model} yet.`)
    const data = kind === 'json' ? exportTemplate(next) : engine.exportFile(next, serializer.value!)
    const blob = new Blob([typeof data === 'string' ? data : new Uint8Array(data).buffer], { type: kind === 'json' ? 'application/json' : 'application/octet-stream' })
    const url = URL.createObjectURL(blob), link = document.createElement('a')
    link.href = url; link.download = `Elixir-${isLaunchpad(deviceId.value) ? 'Launchpad' : 'Launch-Control'}.${kind}`; link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000)
    if (kind === 'json') emit('save-template', next)
    downloadsOpen.value = false; error.value = ''; status.value = 'Downloaded.'
  } catch (e) { error.value = (e as Error).message }
}
async function upload(event: Event) {
  const input = event.target as HTMLInputElement, file = input.files?.[0]; if (!file) return
  try {
    if (file.size > 1_000_000) throw new Error('Template JSON exceeds maximum size')
    if (file.name && !file.name.toLowerCase().endsWith('.json')) throw new Error('Import an editable mapper JSON file. .syx files are device exports; they cannot be restored as editable templates here.')
    const next = importTemplate(await file.text()); ensureAvailable(next); engine.resolve(next)
    next.assignments.forEach(a => { delete a.label })
    template.value = next; deviceId.value = next.deviceProfileId; selected.value = profile.value.controls[0]!.id; error.value = ''; preset.value = 'current'; status.value = 'Configuration imported.'
  } catch (e) { error.value = (e as Error).message }
  input.value = ''
}
const sendDialog = ref<HTMLDialogElement>(), fileInput = ref<HTMLInputElement>()
const sendStarted = ref(false), sendBusy = ref(false), downloadsOpen = ref(false)
async function openSend() {
  sendStarted.value = true
  await nextTick()
  sendDialog.value?.showModal()
}
function closeSend() { if (!sendBusy.value) sendDialog.value?.close() }
const viewport = ref<HTMLElement>(), art = ref<HTMLElement>(), scale = ref(1)
let observer: ResizeObserver | undefined
function resize() {
  if (viewport.value && art.value) scale.value = Math.min((viewport.value.clientWidth - 12) / art.value.offsetWidth, (viewport.value.clientHeight - 12) / art.value.offsetHeight)
}
onMounted(() => { observer = new ResizeObserver(resize); if (viewport.value) observer.observe(viewport.value); if (art.value) observer.observe(art.value); resize() })
onBeforeUnmount(() => observer?.disconnect())
</script>
<template>
  <main class="mx" :class="options.theme?.className" :style="{ color: options.theme?.foreground, background: options.theme?.background, '--mapper-accent': options.theme?.accent }">
    <div class="mx-toolbar">
      <div class="mx-pick">
        <div class="mx-devices" role="group" aria-label="Controller"><button v-for="f in families" :key="f.id" :aria-pressed="!sequencer && family.id === f.id" @click="switchFamily(f.id)">{{ f.name }}</button></div>
        <div v-if="dataUrl" class="mx-devices" role="group" aria-label="Sequencer"><button v-for="q in SEQUENCERS" :key="q.id" :aria-pressed="sequencer === q.id" @click="sequencer = q.id">{{ q.name }}</button></div>
      </div>
      <select class="mx-device-select" :value="deviceChoice" aria-label="Device" @change="chooseDevice(($event.target as HTMLSelectElement).value)"><optgroup label="Controllers"><option v-for="m in devices" :key="m.id" :value="m.id">{{ m.model }}</option></optgroup><optgroup v-if="dataUrl" label="Sequencers"><option v-for="q in SEQUENCERS" :key="q.id" :value="`seq:${q.id}`">{{ q.name }}</option></optgroup></select>
      <template v-if="!sequencer"><select v-if="family.models.length > 1" class="mx-model" :value="deviceId" aria-label="Model" @change="switchDevice(($event.target as HTMLSelectElement).value)"><option v-for="m in family.models" :key="m.id" :value="m.id">{{ modelName(m) }}</option></select>
      <label class="mx-start"><span>Start from</span><select v-model="preset" aria-label="Start from a template" @change="loadPreset"><option value="current">Your edits</option><option value="blank">Nothing (blank)</option><optgroup label="Elixir templates"><option v-for="p in catalog" :key="p.id" :value="p.id">{{ p.name.replace(/^Elixir /, '') }}</option></optgroup><optgroup v-if="saved.length" label="Saved"><option v-for="(t, i) in saved" :key="i" :value="`saved:${i}`">{{ t.name }}</option></optgroup></select></label>
      <div class="mx-files">
        <input ref="fileInput" class="mx-hidden" type="file" accept=".json,application/json" aria-label="Import configuration JSON" @change="upload">
        <div class="mx-menu"><button class="mx-quiet" :aria-expanded="downloadsOpen" aria-controls="mapper-download-options" @click="downloadsOpen = !downloadsOpen">File ▾</button><div v-if="downloadsOpen" id="mapper-download-options" class="mx-menu-list" @keydown.esc="downloadsOpen = false"><button :disabled="!report.valid || !exportable" @click="download('syx')">Save device file (.syx)<small>{{ exportable ? 'for Novation Components' : `not yet for the ${profile.model}` }}</small></button><button @click="download('json')">Save editable config (.json)<small>to open here again</small></button><button @click="downloadsOpen = false; fileInput?.click()">Open editable config…<small>a .json saved from this page</small></button></div></div>
        <button class="mx-primary" :disabled="writable && !report.valid" @click="openSend">{{ writable ? 'Send to controller' : 'Connect & check' }}</button>
      </div></template>
    </div>
    <SequencerPanel v-if="sequencerShown && dataUrl" :sequencer="sequencerShown" :data-url="dataUrl" :tracks-channel="template.channels.remoteTracks" :global-channel="template.channels.remoteGlobal" />
    <div v-else class="mx-layout">
      <div class="mx-stage">
        <div ref="viewport" class="mx-preview"><div ref="art" class="device-art" :style="{ transform: `translate(-50%, -50%) scale(${Math.max(.1, scale)})`, width: isLaunchpad(deviceId) ? '700px' : '790px' }"><ControllerPreview :profile="profile" :assignments="template.assignments.map(a => ({ ...a, label: undefined }))" :selected="selected" :selection="selection" @select="select" @select-many="selectMany" @drop-parameter="assign" /></div></div>
        <p class="mx-count">{{ template.assignments.length }} of {{ profile.controls.length }} controls assigned · drag across controls to select several</p>
        <div class="mx-status" :class="{ 'has-error': error || !report.valid }" :role="error || !report.valid ? 'alert' : 'status'">{{ error || (!report.valid ? report.issues.find(i => i.severity === 'error')?.message : status) }}</div>
      </div>
      <aside class="mx-side">
        <div ref="tabsElement" class="mx-tabs" role="tablist" aria-label="Mapper sidebar"><button v-for="name in TABS" :id="`mapper-tab-${name}`" :key="name" role="tab" :aria-selected="tab === name" :tabindex="tab === name ? 0 : -1" aria-controls="mapper-sidebar-panel" @click="tab = name" @keydown.right.prevent="stepTab(1)" @keydown.left.prevent="stepTab(-1)">{{ TAB_NAMES[name] }}</button></div>
        <div id="mapper-sidebar-panel" class="mx-panel" role="tabpanel" :aria-labelledby="`mapper-tab-${tab}`">
          <template v-if="tab === 'tracks' || tab === 'global'">
            <div v-if="multi" class="mx-banner"><span>{{ targets.length }} controls selected</span><button @click="tab = 'control'">Colors &amp; clear →</button></div>
            <p class="mx-hint">{{ tab === 'tracks' ? `What belongs to a track, on the TRACKS channel (${template.channels.remoteTracks}). Drop one on column 1 to fill the row, D1 to OH.` : `The transport, performance and effects, on the GLOBAL channel (${template.channels.remoteGlobal}).` }} Drag a parameter onto a control, or pick it and click the control.</p>
            <label class="mx-category">Show<select v-model="category" aria-label="Parameter category"><option value="">Everything</option><option v-for="name in categories" :key="name">{{ name }}</option></select></label>
            <div v-for="group in parameterGroups" :key="group.name" class="mx-group"><h2>{{ group.name }} <span>{{ group.items.length }}</span></h2><button v-for="p in group.items" :key="p.id" class="mx-param" draggable="true" :title="p.interpretation" :aria-pressed="picked === p.id" @dragstart="drag($event, p.id)" @click="picked = picked === p.id ? '' : p.id"><span>{{ p.track === 'd1' ? p.name.replace(/^D1 /, '') : p.name }}</span><small>{{ p.track ? 'tracks 1–7' : kindOf(p) }}</small></button><p v-if="!group.items.length" class="mx-hint">Nothing in this category.</p></div>
          </template>
          <template v-else-if="tab === 'control'">
            <template v-if="multi"><h2 class="mx-title">{{ targets.length }} controls selected <button class="mx-link" @click="selectOnly">just {{ control.name }}</button></h2><p class="mx-hint">{{ targetSummary }}</p></template>
            <template v-else>
              <h2 class="mx-title">{{ control.name }}</h2>
              <p v-if="selectedParameter" class="mx-what"><b>{{ selectedParameter.name }}</b><span>{{ kindOf(selectedParameter) }}{{ selectedParameter.channelGroup ? ` · channel ${remoteChannelOf(selectedParameter)}` : '' }}</span>{{ selectedParameter.interpretation }}</p>
              <p v-else class="mx-hint">Nothing assigned. Pick a parameter on the Tracks or Global tab, then click this control.</p>
            </template>
            <template v-for="state in colorStates" :key="state.name"><div v-if="state.enabled" class="mx-colors"><h3>{{ state.name }}</h3><div class="led-swatches"><button v-for="color in state.colors" :key="color" :style="{ background: LED_COLORS[color]?.hex }" :aria-label="`${state.name}: ${LED_COLORS[color]?.name}`" :title="LED_COLORS[color]?.name" :aria-pressed="state.current === color" @click="setColor(color, state.active)" /></div></div></template>
            <p v-if="!colorStates[0]!.enabled" class="mx-hint">{{ multi ? 'These controls have' : 'This control has' }} no light to color.</p>
            <button class="mx-quiet mx-clear" :disabled="!assignedTargets.length" @click="unassign">{{ multi ? `Clear ${assignedTargets.length} assignments` : 'Clear assignment' }}</button>
            <p class="mx-hint">Select several: drag across controls, Shift-click for a range, or {{ isMac ? '⌘' : 'Ctrl' }}-click to add or remove one.</p>
            <div class="mx-section"><label class="mx-check"><input v-model="fillTracks" type="checkbox"> Fill the row from column 1</label><p class="mx-hint">A track parameter dropped on column 1 fills D1 to OH. Column 8 becomes the master volume on knobs and faders; on buttons it stays empty.</p></div>
          </template>
          <template v-else>
            <h2 class="mx-title">Setting up</h2>
            <div class="mx-channels" role="group" aria-label="Elixir's remote channels"><span>Elixir’s remote channels</span><label>TRACKS<input v-model.number="template.channels.remoteTracks" aria-label="Tracks MIDI channel" type="number" min="1" max="16"></label><label>GLOBAL<input v-model.number="template.channels.remoteGlobal" aria-label="Global MIDI channel" type="number" min="1" max="16"></label></div>
            <ol class="mx-steps">
              <li><b>On the FM-1:</b> GLO twice opens CONFIG; turn SELECT to <b>REMOTE</b>. Set Tracks to <b>{{ template.channels.remoteTracks }}</b> and Global to <b>{{ template.channels.remoteGlobal }}</b>, as above (other channels: change them here first). Knobs: <b>PICKUP</b> for knobs and faders (nothing jumps), REL for endless encoders. Mute/solo: <b>PRESS</b>, or TOGGLE for buttons set to toggle.</li>
              <li v-if="profile.id === LCXL_MK2_PROFILE.id"><b>Onto the controller:</b> <b>Send to controller</b> writes one of its eight user templates from Chrome or Edge, and selects it. Or save the device file (.syx) and send it with Novation Components.</li>
              <li v-else-if="writable"><b>Onto the controller:</b> <b>Send to controller</b> writes custom mode 1 from Chrome or Edge, after a backup. For another mode, save the device file (.syx) and send it with Novation Components.</li>
              <li v-else-if="exportable"><b>Onto the controller:</b> <b>File ▾ → Save device file (.syx)</b>, then in Novation Components import it and send it to a user template. Select that template on the {{ profile.model }}; <b>Connect &amp; check</b> then shows whether each control sends what Elixir expects.</li>
              <li v-else><b>Onto the controller:</b> this page can't write the {{ profile.model }} yet. In Novation Components, open one of its user templates and set each control as listed below; then <b>Connect &amp; check</b> shows whether each one sends what Elixir expects.</li>
              <li><b>Connecting:</b> both are USB devices, so they meet at a USB host (a computer, or a USB MIDI host box), or the controller's MIDI out goes to the FM-1's TRS input. The lights and knob rings follow Elixir only over USB.</li>
            </ol>
            <details class="mx-section mx-sheet" :open="!writable && !exportable"><summary>The template as a list <span>{{ sheet.length }}</span></summary>
              <table><thead><tr><th>Control</th><th>Elixir</th><th>Sends</th><th>Ch</th></tr></thead><tbody><tr v-for="r in sheet" :key="r.id"><td>{{ r.control }}</td><td>{{ r.name }}</td><td>{{ r.message }}{{ r.mode === 'toggle' ? ' · toggle' : '' }}</td><td>{{ r.channel }}</td></tr></tbody></table>
              <p class="mx-hint">Knobs and faders: absolute, 0–127. Buttons: momentary unless marked.</p>
            </details>
            <p class="mx-hint">The manual explains every control: <a href="../manual/#the-remote-map">the remote map</a>. The <a href="../cheatsheet.pdf">cheat sheet</a>'s last page has it on one page.</p>
            <div class="mx-section"><h3>Elixir's parameters</h3><p class="mx-hint">Read from the firmware of Elixir {{ REGISTRY_SOURCE.firmwareVersion }} ({{ REGISTRY_SOURCE.sourceRevision }}). The parameter list is the remote map as data, not a controller configuration.</p><span v-if="options.parameterJsonUrl" class="mx-links"><a :href="options.parameterJsonUrl" download>Parameter JSON ↓</a><a :href="options.parameterJsonUrl.replace(/\.json$/, '.csv')" download>CSV ↓</a></span></div>
          </template>
        </div>
      </aside>
    </div>
    <dialog ref="sendDialog" class="mx-dialog" aria-labelledby="send-dialog-title" @cancel.prevent="closeSend">
      <div class="mx-dialog-head"><h2 id="send-dialog-title">{{ writable ? 'Send to' : 'Connect' }} the {{ profile.model }}</h2><button aria-label="Close send panel" :disabled="sendBusy" @click="closeSend">×</button></div>
      <p v-if="profile.id === LCXL_MK2_PROFILE.id" class="mx-hint">Close Novation Components first. Connect, pick the user template to replace, confirm, and send. Nothing is sent before you do.</p>
      <p v-else-if="writable" class="mx-hint">Close Novation Components first. Connect, back up the mode you'll replace, then confirm. Nothing is sent before you do.</p>
      <p v-else class="mx-hint">This page doesn't write to the {{ profile.model }}: send it the device file (File ▾) with Novation Components. Here you can check it: connect, choose its input, and move the controls it asks for.</p>
      <HardwareConnection v-if="sendStarted" :key="family.id" :template="template" compact @busy="sendBusy = $event" @destination-slot="template.destinationSlot = $event" @use-model="useModel" />
    </dialog>
  </main>
</template>
<style scoped>
/* The site's look (site.css, loaded as a layer under these: its tokens, light and dark). The controller drawing
   (ControllerPreview) stays dark: it's the hardware. Fallbacks for a page without site.css (tests, npm run dev). */
.mx{--mx-accent:var(--mapper-accent,var(--accent-fill,#f0a43a));box-sizing:border-box;height:calc(100dvh - var(--host-nav-height,0px));display:flex;flex-direction:column;overflow:hidden;margin:0;padding:0;max-width:none;
  background:var(--bg,#131210);color:var(--fg,#f2ede3);font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI","Helvetica Neue",Arial,sans-serif}
.mx *{box-sizing:border-box}
.mx :where(button,input,select){font:inherit;color:inherit;line-height:1.2;font-weight:inherit}
.mx :where(button){cursor:pointer}
.mx button:disabled{opacity:.45;cursor:default}
.mx :focus-visible{outline:2px solid var(--mx-accent);outline-offset:2px}
.mx a{color:var(--accent,#f5b04f)}
.mx-hidden{display:none}

/* the toolbar, under the site's header */
.mx-toolbar{display:flex;align-items:center;flex-wrap:wrap;gap:8px 14px;padding:8px 16px;border-bottom:1px solid var(--line,#2d2a25);flex-shrink:0}
.mx-devices{display:flex;padding:3px;gap:3px;border:1px solid var(--line,#2d2a25);border-radius:9px;background:var(--well,#201e1b)}
.mx-devices button{border:0;background:none;padding:5px 10px;border-radius:6px;color:var(--dim,#a29b8e);font-size:14px;white-space:nowrap}
.mx-devices [aria-pressed=true]{background:var(--card,#1b1a17);color:var(--fg,#f2ede3);font-weight:600;box-shadow:0 1px 2px #0003}
.mx-pick{display:flex;gap:8px}
.mx-device-select{display:none}
.mx-start,.mx-channels,.mx-channels label{display:flex;align-items:center;gap:8px;font-size:14px;color:var(--dim,#a29b8e)}
.mx-channels{flex-wrap:wrap;gap:8px 12px;margin:4px 0 12px}.mx-channels>span{width:100%;font-size:13px}.mx-channels label{gap:6px;font-size:12px;letter-spacing:.04em}
.mx select,.mx-channels input{background:var(--card,#1b1a17);border:1px solid var(--line,#2d2a25);border-radius:7px;padding:6px 8px;font-size:14px;color:var(--fg,#f2ede3)}
.mx-channels input{width:52px}
.mx-files{display:flex;align-items:center;gap:8px;margin-left:auto}
.mx-quiet{border:1px solid var(--line,#2d2a25);background:var(--card,#1b1a17);border-radius:8px;padding:7px 12px;font-size:14px}
.mx-quiet:hover:not(:disabled){border-color:var(--dim,#a29b8e)}
.mx-primary{border:1px solid var(--accent-fill,#f0a43a);background:var(--accent-fill,#f0a43a);color:var(--accent-ink,#1d1b18);border-radius:8px;padding:8px 16px;font-size:14px;font-weight:600}
.mx-menu{position:relative}
.mx-menu-list{position:absolute;right:0;top:calc(100% + 6px);z-index:10;width:230px;padding:5px;background:var(--card,#1b1a17);border:1px solid var(--line,#2d2a25);border-radius:9px;box-shadow:0 12px 28px #0004}
.mx-menu-list button{display:block;width:100%;text-align:left;border:0;background:none;padding:9px 10px;border-radius:6px;font-size:14px}
.mx-menu-list button:hover:not(:disabled){background:var(--well,#201e1b)}
.mx-menu-list small{display:block;color:var(--dim,#a29b8e);font-size:12px;margin-top:2px}

/* the controller, and the side bar */
.mx-layout{display:grid;grid-template-columns:minmax(0,1fr) 330px;flex:1;min-height:0}
.mx-stage{position:relative;display:flex;flex-direction:column;min-width:0;min-height:0;background:var(--well,#201e1b)}
.mx-preview{flex:1;min-height:0;position:relative;overflow:hidden}
.device-art{position:absolute;left:50%;top:50%;transform-origin:center}
.mx-preview :deep(.device-scroll){padding:4px;overflow:hidden}
.mx-preview :deep(.preview-note),.mx-preview :deep(.device-caption){display:none}
.mx-preview :deep(.device-shell){max-width:none}
.mx-count{margin:0;padding:0 16px 10px;font-size:13px;color:var(--dim,#a29b8e);text-align:center}
.mx-status{position:absolute;width:1px;height:1px;overflow:hidden;clip-path:inset(50%);white-space:nowrap}
.mx-status.has-error{clip-path:none;white-space:normal;width:auto;height:auto;left:16px;right:16px;bottom:40px;z-index:3;padding:10px 12px;border-radius:8px;font-size:14px;background:var(--card,#1b1a17);color:var(--warn,#ff9a5c);border:1px solid var(--warn,#ff9a5c)}
.mx-side{display:flex;flex-direction:column;min-height:0;border-left:1px solid var(--line,#2d2a25);background:var(--bg,#131210)}
.mx-tabs{display:flex;gap:4px;padding:0 12px;border-bottom:1px solid var(--line,#2d2a25)}
.mx-tabs button{flex:1;border:0;background:none;padding:12px 0 10px;font-size:14px;color:var(--dim,#a29b8e);border-bottom:2px solid transparent}
.mx-tabs [aria-selected=true]{color:var(--fg,#f2ede3);font-weight:600;border-bottom-color:var(--mx-accent)}
.mx-panel{overflow-y:auto;min-height:0;padding:16px 16px 28px;scrollbar-width:thin}
.mx-hint{font-size:13px;line-height:1.5;color:var(--dim,#a29b8e);margin:8px 0 14px}
.mx-title{display:flex;align-items:baseline;flex-wrap:wrap;gap:8px;font-size:18px;font-weight:650;margin:0 0 6px}
.mx-link{border:0;background:none;padding:0;color:var(--accent,#f5b04f);font-size:13px}
.mx-link:hover,.mx-banner button:hover{text-decoration:underline}
.mx-banner{display:flex;align-items:center;justify-content:space-between;gap:8px;margin:0 0 12px;padding:8px 10px;border-radius:8px;font-size:14px;background:color-mix(in srgb,var(--mx-accent) 16%,transparent);border:1px solid color-mix(in srgb,var(--mx-accent) 55%,transparent)}
.mx-banner button{border:0;background:none;padding:0;color:var(--accent,#f5b04f);font-size:14px;font-weight:600}
.mx-category{display:flex;align-items:center;gap:10px;font-size:14px;color:var(--dim,#a29b8e);margin-bottom:16px}
.mx-category select{flex:1}
.mx-group{margin-bottom:20px}
.mx-group h2{display:flex;font-size:12px;font-weight:600;letter-spacing:.08em;text-transform:uppercase;color:var(--dim,#a29b8e);margin:0 0 8px}
.mx-group h2 span{margin-left:auto;font-weight:400}
.mx-param{display:flex;justify-content:space-between;align-items:center;gap:8px;width:100%;margin-bottom:5px;padding:8px 10px;border:1px solid var(--line,#2d2a25);border-radius:8px;background:var(--card,#1b1a17);text-align:left;font-size:14px;cursor:grab!important}
.mx-param:hover,.mx-param[aria-pressed=true]{border-color:var(--mx-accent)}
.mx-param[aria-pressed=true]{background:color-mix(in srgb,var(--mx-accent) 14%,var(--card,#1b1a17))}
.mx-param small{flex-shrink:0;font-size:12px;color:var(--dim,#a29b8e)}
.mx-what{font-size:14px;line-height:1.5;margin:4px 0 6px;padding:10px 12px;border-radius:8px;background:var(--well,#201e1b)}
.mx-what b{display:block}.mx-what span{display:block;font-size:12px;color:var(--dim,#a29b8e);margin-bottom:4px}
.mx-colors h3{font-size:13px;font-weight:600;margin:16px 0 8px}
.mx-colors .led-swatches{display:flex;flex-wrap:wrap;gap:8px}
.mx-colors .led-swatches button{width:24px;height:24px;padding:0;border-radius:50%;border:2px solid color-mix(in srgb,var(--fg,#f2ede3) 18%,transparent)}
.mx-colors [aria-pressed=true]{outline:2px solid var(--fg,#f2ede3);outline-offset:2px}
.mx-clear{margin-top:16px}
.mx-section{margin-top:18px;padding-top:16px;border-top:1px solid var(--line,#2d2a25)}
.mx-section h3{font-size:14px;margin:0 0 4px}
.mx-check{display:flex;align-items:center;gap:8px;font-size:14px}
.mx-check input{accent-color:var(--mx-accent)}
.mx-steps{margin:8px 0 6px;padding-left:20px;font-size:14px;line-height:1.55}
.mx-steps li{margin-bottom:10px}
.mx-model{padding:6px 8px;border:1px solid var(--line,#2d2a25);border-radius:8px;background:var(--card,#1b1a17);font-size:14px}
.mx-sheet summary{cursor:pointer;font-size:14px;font-weight:600}
.mx-sheet summary span{color:var(--dim,#a29b8e);font-weight:400;margin-left:4px}
.mx-sheet table{width:100%;border-collapse:collapse;font-size:12.5px;margin-top:8px}
.mx-sheet th{text-align:left;font-weight:600;color:var(--dim,#a29b8e)}
.mx-sheet :is(th,td){padding:3px 6px 3px 0;border-bottom:1px solid var(--line,#2d2a25);vertical-align:top}
.mx-sheet td:nth-child(3){white-space:nowrap;font-variant-numeric:tabular-nums}
.mx-links{display:flex;gap:16px;font-size:14px;font-weight:600}

/* the send dialog */
.mx-dialog{width:min(680px,calc(100vw - 32px));max-height:calc(100dvh - 40px);overflow:auto;padding:22px;border-radius:12px;border:1px solid var(--line,#2d2a25);background:var(--bg,#131210);color:var(--fg,#f2ede3);box-shadow:0 24px 80px #0007}
.mx-dialog::backdrop{background:#0008;backdrop-filter:blur(2px)}
.mx-dialog-head{display:flex;align-items:center;justify-content:space-between;gap:16px}
.mx-dialog-head h2{font-size:20px;margin:0}
.mx-dialog-head button{border:1px solid var(--line,#2d2a25);background:var(--card,#1b1a17);border-radius:7px;font-size:20px;padding:0 10px}
.mx-dialog :deep(.hardware-panel){margin:0;padding:0;border:0;background:none}
.mx-dialog :deep(p){font-size:14px;color:var(--dim,#a29b8e)}
.mx-dialog :deep(button),.mx-dialog :deep(select),.mx-dialog :deep(input){font-size:14px;border:1px solid var(--line,#2d2a25);background:var(--card,#1b1a17);color:var(--fg,#f2ede3);border-radius:7px;padding:8px 10px}
.mx-dialog :deep(.actions){display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin:10px 0}
.mx-dialog :deep(.pair){display:flex;flex-wrap:wrap;gap:12px}
.mx-dialog :deep(label){display:flex;flex-direction:column;gap:4px;font-size:13px;color:var(--dim,#a29b8e)}
.mx-dialog :deep(.overwrite-confirmation){flex-direction:row;gap:10px;color:var(--fg,#f2ede3)}
.mx-dialog :deep(h3){font-size:16px;margin:18px 0 6px}
.mx-dialog :deep(.error){color:var(--warn,#ff9a5c);border:1px solid var(--warn,#ff9a5c);border-radius:7px;padding:8px 10px}

@media (max-width:1100px){.mx-start>span{display:none}}
@media (max-width:900px){.mx-layout{grid-template-columns:minmax(0,1fr) 280px}}
@media (max-width:980px){.mx-pick,.mx-model{display:none}.mx-device-select{display:block;max-width:100%}}
@media (max-width:640px){
  .mx{height:auto;min-height:calc(100dvh - var(--host-nav-height,0px));overflow:visible}
  .mx-layout{grid-template-columns:1fr;grid-template-rows:minmax(260px,46dvh) auto}
  .mx-side{border-left:0;border-top:1px solid var(--line,#2d2a25)}
  .mx-panel{overflow:visible}
  .mx-toolbar{gap:8px}.mx-device-select,.mx-start{flex:1 1 140px;min-width:0}.mx-start select{width:100%;min-width:0}
  .mx-files{flex:1 1 100%;justify-content:flex-end}
}
</style>
