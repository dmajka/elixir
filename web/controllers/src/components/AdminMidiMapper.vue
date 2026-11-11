<script setup lang="ts">
import { computed, ref, provide, watch } from 'vue'
import { assignTrackRow } from '../elixir/assignment-tools'
import { LED_COLORS } from './led-colors'
import HardwareConnection from './HardwareConnection.vue'
import ControllerPreview from './ControllerPreview.vue'
import ParameterBrowser from './ParameterBrowser.vue'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { LAUNCHPAD_PRO_MK3_FILE_SERIALIZER } from '../devices/novation/launchpad-pro-mk3/adapter'
import { LAUNCHPAD_TEMPLATE_CATALOG, generateLaunchpadTemplate, type LaunchpadTemplateId } from '../elixir/launchpad-templates'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { createEmptyTemplate } from '../elixir/mapping-context'
import { TEMPLATE_CATALOG, generateTemplate, type BuiltinTemplateId } from '../elixir/templates'
import { exportTemplate, importTemplate } from '../core/mapping-engine/schema'
import type { ControllerTemplate, MidiAssignment } from '../core/mapping-engine/types'
import { createMapperContext, mapperContextKey, type MidiMapperOptions } from '../integration/context'
import { validateChannelSettings, DEFAULT_CHANNEL_SETTINGS } from '../elixir/channel-settings'
import { detectMidiSupport } from '../midi/provider'
const props = defineProps<{ options?: MidiMapperOptions }>()
const emit = defineEmits<{ 'template-change': [template: ControllerTemplate]; 'save-template': [template: ControllerTemplate] }>()
// Configuration is a mount-time snapshot. Remount to change it safely.
const options = structuredClone(props.options ? JSON.parse(JSON.stringify(props.options)) : {}) as MidiMapperOptions
const context = createMapperContext(options.parameterOverrides)
provide(mapperContextKey, context)
const { engine } = context, support = detectMidiSupport()
const devices = [XL3_PROFILE, LAUNCHPAD_PRO_MK3_PROFILE].filter(p => !options.availableDevices || options.availableDevices.includes(p.id))
if (!devices.length) throw new Error('Enable at least one supported mapper device')
const channels = options.defaultChannels ?? DEFAULT_CHANNEL_SETTINGS
validateChannelSettings(channels)
function ensureAvailable(t: ControllerTemplate) { if (!devices.some(p => p.id === t.deviceProfileId)) throw new Error('Template device is unavailable') }
const initial = devices[0]!.id === XL3_PROFILE.id ? generateTemplate('mixer', channels) : generateLaunchpadTemplate('pad-mute-solo', channels)
const template = ref(initial), selected = ref(devices[0]!.controls[0]!.id), preset = ref(devices[0]!.id === XL3_PROFILE.id ? 'mixer' : 'pad-mute-solo')
const users = ref<ControllerTemplate[]>((options.savedTemplates ?? []).map(t => { const next = copy(t); ensureAvailable(next); engine.resolve(next); return next })), message = ref(''), error = ref('')
const draft = ref<MidiAssignment>({ ...template.value.assignments[0]! })
function snapshot(): ControllerTemplate { return JSON.parse(JSON.stringify(template.value)) }
const report = computed(() => engine.validate(snapshot()))
const profile = computed(() => template.value.deviceProfileId === LAUNCHPAD_PRO_MK3_PROFILE.id ? LAUNCHPAD_PRO_MK3_PROFILE : XL3_PROFILE)
const catalog = computed(() => profile.value.id === XL3_PROFILE.id ? TEMPLATE_CATALOG : LAUNCHPAD_TEMPLATE_CATALOG)
const control = computed(() => profile.value.controls.find(c => c.id === selected.value)!)
const isButton = computed(() => ['button', 'pad'].includes(control.value.kind))
const resolved = computed(() => { try { return engine.resolve(snapshot()) } catch { return undefined } })
const current = computed(() => resolved.value?.assignments.find(a => a.control.id === selected.value))
function select(id: string) {
  selected.value = id
  draft.value = JSON.parse(JSON.stringify(template.value.assignments.find(a => a.controlId === id) ?? { controlId: id, parameterId: '' }))
  error.value = ''
}
// Vue refs contain proxies: use schema serialization to obtain a portable, detached copy.
function copy(t: ControllerTemplate) { return importTemplate(exportTemplate(t)) }
function load() {
  try {
    const channels = { remoteTracks: template.value.channels.remoteTracks!, remoteGlobal: template.value.channels.remoteGlobal! }
    template.value = preset.value.startsWith('user:') ? copy(users.value[Number(preset.value.slice(5))]!) : profile.value.id === XL3_PROFILE.id ? generateTemplate(preset.value as BuiltinTemplateId, channels) : generateLaunchpadTemplate(preset.value as LaunchpadTemplateId, channels)
    select(profile.value.controls.some(c => c.id === selected.value) ? selected.value : profile.value.controls[0]!.id); message.value = 'Template loaded.'
  } catch (e) { error.value = (e as Error).message }
}
function newTemplate() {
  try {
    const next = createEmptyTemplate('user.new', 'My Template', profile.value.id)
    next.channels = { ...template.value.channels }
    template.value = next; saveUser(); select(profile.value.controls[0]!.id)
  } catch (e) { error.value = (e as Error).message }
}
function defaultRange() {
  const p = draft.value.parameterId ? context.parameters.find(p => p.id === draft.value.parameterId) : undefined
  return { minimum: p?.minimum ?? 0, maximum: p?.maximum ?? 127 }
}
const autoFillTracks = ref(true)
const canFillRow = computed(() => /^tracks\.(d1|d2|d3|sn|cy|ch|oh|selected)\./.test(draft.value.parameterId))
function fillRow() {
  try {
    const next = assignTrackRow(snapshot(), profile.value, context.parameters, selected.value, draft.value.parameterId)
    engine.resolve(next); template.value = next; select(selected.value)
    message.value = 'Applied to tracks 1–7. Column 8 is master volume for knobs/sliders, or unassigned for buttons/pads.'
  } catch (e) { error.value = (e as Error).message }
}
function dropParameter(controlId: string, parameterId: string) {
  try {
    const target = profile.value.controls.find(c => c.id === controlId)
    if (autoFillTracks.value && target?.column === 0 && /^tracks\.(d1|d2|d3|sn|cy|ch|oh|selected)\./.test(parameterId)) {
      const next = assignTrackRow(snapshot(), profile.value, context.parameters, controlId, parameterId)
      engine.resolve(next); template.value = next; select(controlId)
      message.value = 'Assigned tracks 1–7 across this row; column 8 reserved for master.'
      return
    }
    const next = copy(template.value)
    next.assignments = next.assignments.filter(a => a.controlId !== controlId)
    next.assignments.push({ controlId, parameterId })
    engine.resolve(next); template.value = next; select(controlId)
    message.value = 'Parameter assigned. Use Apply across tracks 1–7 to fill this row.'
  } catch (e) { error.value = (e as Error).message }
}
function choose(parameterId: string) { draft.value = { controlId: selected.value, parameterId }; error.value = '' }
function apply(remove = false) {
  try {
    const next = copy(template.value)
    next.assignments = next.assignments.filter(a => a.controlId !== selected.value)
    if (!remove) next.assignments.push(JSON.parse(JSON.stringify(draft.value)))
    engine.resolve(next); template.value = next; select(selected.value); message.value = remove ? 'Control unassigned.' : 'Assignment applied.'
  } catch (e) { error.value = (e as Error).message }
}
function saveUser() {
  try {
    engine.resolve(snapshot())
    const saved = copy(template.value); saved.id = `user.${users.value.length + 1}`
    users.value.push(saved); emit('save-template', copy(saved)); preset.value = `user:${users.value.length - 1}`; message.value = 'Saved in this session. Download template JSON to keep it.'; error.value = ''
  } catch (e) { error.value = (e as Error).message }
}
function download(kind: 'syx' | 'json') {
  try {
    const data = kind === 'syx' ? engine.exportFile(snapshot(), (profile.value.id === XL3_PROFILE.id ? XL3_FILE_SERIALIZER : LAUNCHPAD_PRO_MK3_FILE_SERIALIZER)) : exportTemplate(template.value)
    const blob = new Blob([typeof data === 'string' ? data : new Uint8Array(data).buffer], { type: kind === 'syx' ? 'application/octet-stream' : 'application/json' })
    const url = URL.createObjectURL(blob), a = document.createElement('a')
    a.href = url; a.download = `${template.value.name.replace(/[^a-z0-9_-]/gi, '-')}.${kind}`; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000)
    message.value = `Downloaded ${a.download}`; error.value = ''
  } catch (e) { error.value = (e as Error).message }
}
async function upload(event: Event) {
  const input = event.target as HTMLInputElement, file = input.files?.[0]
  if (!file) return
  try {
    if (file.size > 1_000_000) throw new Error('Template JSON exceeds maximum size')
    const next = importTemplate(await file.text()); ensureAvailable(next); engine.resolve(next)
    template.value = next; saveUser(); select(profile.value.controls[0]!.id)
  } catch (e) { error.value = (e as Error).message }
  input.value = ''
}
function switchDevice(event: Event) {
  try {
  const id = (event.target as HTMLSelectElement).value
  const channels = { remoteTracks: template.value.channels.remoteTracks!, remoteGlobal: template.value.channels.remoteGlobal! }
  template.value = id === XL3_PROFILE.id ? generateTemplate('mixer', channels) : generateLaunchpadTemplate('pad-mute-solo', channels)
  preset.value = id === XL3_PROFILE.id ? 'mixer' : 'pad-mute-solo'
  select(profile.value.controls[0]!.id); message.value = 'Controller loaded.'
  } catch (e) {
    (event.target as HTMLSelectElement).value = profile.value.id
    error.value = (e as Error).message
  }
}
watch(template, () => emit('template-change', snapshot()), { deep: true })
</script>

<template>
  <main class="mapper" :class="options.theme?.className" :style="{ color: options.theme?.foreground, background: options.theme?.background, '--mapper-accent': options.theme?.accent }">
    <header><p class="eyebrow">ELIXIR / MIDI TOOLS</p><h1>MIDI Mapper</h1><p>Build a controller around the way you play.</p><span class="badge">File export workspace</span></header>
    <section class="setup" aria-label="Template setup">
      <label>Controller<select aria-label="Controller" :value="profile.id" @change="switchDevice"><option v-for="device in devices" :key="device.id" :value="device.id">{{ device.model }}</option></select></label>
      <label>Template<select v-model="preset" @change="load"><option v-for="p in catalog" :key="p.id" :value="p.id">{{ p.id === 'sound-editor' ? 'Elixir Sound Editor' : p.id === 'performance' ? 'Elixir Performance' : p.name }}</option><option v-for="(u, i) in users" :key="i" :value="`user:${i}`">{{ u.name }} (user)</option></select></label>
      <label>Mode name<input v-model="template.name" :maxlength="profile.text.mode.maximumLength"></label>
      <label>Remote Tracks channel<input v-model.number="template.channels.remoteTracks" type="number" min="1" max="16"></label>
      <label>Remote Global channel<input v-model.number="template.channels.remoteGlobal" type="number" min="1" max="16"></label>
      <label>Elixir encoder setting<select v-model="template.receiver.encoderMode"><option value="pickup">PICKUP</option><option value="absolute">ABS</option><option value="relative">REL</option></select></label>
      <label>Elixir button setting<select v-model="template.receiver.buttonMode"><option value="press">PRESS</option><option value="state">TOGGLE / state</option></select></label>
    </section>
    <p>{{ catalog.find(p => p.id === preset)?.description }}</p>
    <p class="compatibility">{{ profile.model }}: {{ profile.protocol.fileExport === 'verified' ? 'Components file compatibility verified.' : 'File compatibility validation pending.' }} {{ profile.limitations.join(' ') }} Set matching remote channels and receiving modes in Elixir. {{ support.reason }}</p>
    <div v-if="error" class="error" role="alert">{{ error }}</div><p role="status" aria-live="polite">{{ message }}</p>
    <div class="workspace">
      <section class="preview-panel"><div class="section-heading"><h2>Controller preview</h2><span>{{ template.assignments.length }}/{{ profile.controls.length }} assigned</span></div><p>Click a control to edit. Changes apply when you press Apply assignment.</p>
        <ControllerPreview :profile="profile" :assignments="template.assignments" :selected="selected" @select="select" @drop-parameter="dropParameter" />
      </section>
      <section class="editor" aria-label="Mapping editor">
        <h2>{{ control.name }}</h2><p v-if="current">{{ current.parameter.name }} · {{ current.messageType }} {{ current.number }} · channel {{ current.channel }}</p><p v-else>Unassigned — disabled in exported mode.</p>
        <form @submit.prevent="apply()">
          <label>Parameter ID<input v-model="draft.parameterId" readonly placeholder="Choose below"></label>
          <label>Control label<input v-model="draft.label" maxlength="31" placeholder="Use parameter name"></label>
          <label>Channel override<input :value="draft.channelOverride ?? ''" type="number" min="1" max="16" placeholder="Use remote channel" @input="draft.channelOverride = ($event.target as HTMLInputElement).value === '' ? undefined : Number(($event.target as HTMLInputElement).value)"></label>
          <label v-if="isButton">Message<select v-model="draft.messageType"><option :value="undefined">Parameter default</option><option value="cc">CC</option><option value="note">Note</option><option value="program-change">Program Change</option></select></label>
          <label v-if="isButton">Button behavior<select v-model="draft.buttonMode"><option :value="undefined">Momentary (default)</option><option value="momentary">Momentary</option><option value="toggle">Toggle</option></select></label>
          <label v-else>Encoder behavior<select v-model="draft.encoderMode"><option :value="undefined">Absolute (default)</option><option v-for="mode in control.encoderModes" :key="mode" :value="mode">{{ mode }}</option></select></label>
          <fieldset v-if="!isButton"><legend>Value range</legend><label><input type="checkbox" :checked="!!draft.range" @change="draft.range = ($event.target as HTMLInputElement).checked ? defaultRange() : undefined"> Override range</label><div v-if="draft.range" class="pair"><label>Minimum<input v-model.number="draft.range.minimum" type="number" min="0" max="127"></label><label>Maximum<input v-model.number="draft.range.maximum" type="number" min="0" max="127"></label></div></fieldset>
          <div class="pair"><label v-if="control.led.off">LED color<select :value="draft.colors?.off ?? ''" @change="draft.colors = { ...draft.colors, off: ($event.target as HTMLSelectElement).value === '' ? undefined : Number(($event.target as HTMLSelectElement).value) }"><option value="">Default</option><option v-for="color in control.led.colors" :key="color" :value="color">{{ LED_COLORS[color]?.name ?? 'Custom color' }}</option></select></label><label v-if="control.led.on">Active color<select :value="draft.colors?.on ?? ''" @change="draft.colors = { ...draft.colors, on: ($event.target as HTMLSelectElement).value === '' ? undefined : Number(($event.target as HTMLSelectElement).value) }"><option value="">Default</option><option v-for="color in control.led.colors" :key="color" :value="color">{{ LED_COLORS[color]?.name ?? 'Custom color' }}</option></select></label></div>
          <div v-if="control.led.off" class="led-swatches" aria-label="LED color palette"><button v-for="color in control.led.colors" :key="color" type="button" :aria-label="`LED color: ${LED_COLORS[color]?.name}`" :aria-pressed="draft.colors?.off === color" :title="LED_COLORS[color]?.name" :style="{ background: LED_COLORS[color]?.hex }" @click="draft.colors = { ...draft.colors, off: color }" /></div>
          <div class="actions"><button class="primary" type="submit" :disabled="!draft.parameterId">Apply assignment</button><button type="button" @click="apply(true)">Unassign</button></div>
        </form>
        <div class="track-row-action"><label class="row-fill-toggle"><input v-model="autoFillTracks" type="checkbox"> Auto-fill tracks 1–7 when dropping a track parameter on column 1</label><button type="button" :disabled="!canFillRow" @click="fillRow">Apply across tracks 1–7</button><p>Replaces the selected row with D1, D2, D3, SN, CY, CH and OH. Column 8 becomes master volume on knobs/sliders; buttons/pads stay unassigned because Elixir has no master mute/solo.</p></div>
        <ParameterBrowser :control="control" :channels="template.channels" @choose="choose" />
      </section>
    </div>
    <section class="export-panel"><h2>Validate &amp; export</h2><p>{{ report.valid ? 'Mapping is valid.' : 'Resolve errors before exporting.' }} {{ report.unassignedControlIds.length }} unassigned controls.</p><ul class="issues"><li v-for="(issue, i) in report.issues" :key="i" :class="issue.severity">{{ issue.severity }}: {{ issue.message }} {{ issue.controlId }}</li></ul><details v-if="report.unassignedControlIds.length"><summary>Unassigned controls</summary>{{ report.unassignedControlIds.join(', ') }}</details>
      <div class="actions"><button class="primary" :disabled="!report.valid" @click="download('syx')">Download .syx</button><button :disabled="!report.valid" @click="saveUser">Save user template</button><button @click="newTemplate">New blank template</button><button @click="download('json')">Download template JSON</button><label class="file-button">Import template JSON<input type="file" accept=".json,application/json" @change="upload"></label></div><p>Loading a template replaces the current workspace. Save or download your edits first. User templates last for this session; JSON files preserve them for later.</p>
    </section>
    <HardwareConnection :key="profile.id" :template="template" />
    <footer>Standalone mapper · Direct XL3 upload: slot 1, firmware build 1563, absolute CC</footer>
  </main>
</template>
