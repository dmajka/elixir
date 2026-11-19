<script setup lang="ts">
import MidiMonitor from './MidiMonitor.vue'
import ControllerCheck from './ControllerCheck.vue'
import LaunchpadTransfer from './LaunchpadTransfer.vue'
import Mk2Send from './Mk2Send.vue'
import SlotSend from './SlotSend.vue'
import { LAUNCHPAD_PRO_MK3_PROFILE } from '../devices/novation/launchpad-pro-mk3/profile'
import { computed, onBeforeUnmount, ref, shallowRef, watch } from 'vue'
import type { MidiTransactionTrace } from '../midi/request-transport'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import { createWebMidiSession, type CapturedMidiMessage } from '../midi/web-midi'
import { detectMidiSupport, type MidiPort } from '../midi/provider'
import { XL3_PROFILE } from '../devices/novation/launch-control-xl3/profile'
import { detectModels, familyOf, profileById } from '../devices/families'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { useMapperContext } from '../integration/context'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { inspectXL3TransferFile } from '../devices/novation/launch-control-xl3/transfer'
import { createXL3Uploader, isXL3MainPortName, XL3_UPLOAD_LIMIT, XL3_UPLOAD_EXPERIMENT_ENABLED, type UploadProgress } from '../devices/novation/launch-control-xl3/upload'
const props = defineProps<{ template: ControllerTemplate; compact?: boolean }>()
const emit = defineEmits<{ busy: [value: boolean]; 'destination-slot': [value: number]; 'use-model': [id: string] }>()
const launchpadBusy = ref(false), restoreAvailable = ref(false)
const deviceProfile = computed(() => profileById(props.template.deviceProfileId) ?? XL3_PROFILE)
const isMk2 = computed(() => props.template.deviceProfileId === LCXL_MK2_PROFILE.id)
const choosePorts = ref(false)   // (compact: the ports are picked by name; shown only to change them)
const support = detectMidiSupport(), session = createWebMidiSession(), engine = useMapperContext().engine
const ports = ref<MidiPort[]>([]), connected = ref(false), busy = ref(false), error = ref('')
const inputId = ref(''), outputId = ref(''), slot = ref(props.template.destinationSlot ?? 1), messages = ref<CapturedMidiMessage[]>([])
const latestMessage = shallowRef<CapturedMidiMessage>()
const capture = ref(false), captureFull = ref(false), excludeClock = ref(false)
const transactionTrace = ref<MidiTransactionTrace[]>([]), traceTruncated = ref(false)
const traceContext = shallowRef<{ inputId: string; outputId: string; slot: number; modeName: string }>()
const traceProgress = shallowRef<UploadProgress>()
watch(() => busy.value || launchpadBusy.value, value => emit('busy', value), { flush: 'sync' })
watch(() => props.template.destinationSlot, value => { slot.value = value ?? 1 })
watch(slot, value => { if (props.compact) emit('destination-slot', value) })
const inputs = computed(() => ports.value.filter(p => p.type === 'input'))
const outputs = computed(() => ports.value.filter(p => p.type === 'output'))
const preparation = computed(() => {
  try { return { value: inspectXL3TransferFile(engine.exportFile(JSON.parse(JSON.stringify(props.template)), XL3_FILE_SERIALIZER), slot.value), error: '' } }
  catch (e) { return { value: undefined, error: (e as Error).message } }
})
let uploader: ReturnType<typeof createXL3Uploader> | undefined
let transaction: AbortController | undefined
let requestTransport: ReturnType<typeof session.createRequestTransport> | undefined
const readPlan = shallowRef<Awaited<ReturnType<ReturnType<typeof createXL3Uploader>['prepare']>>>()
const confirmed = ref(false), backupDownloaded = ref(false)
const progress = ref<UploadProgress>({ phase: 'idle', sentSurfaces: 0, acknowledgedSurfaces: 0, stored: false, message: '' })
function invalidatePlan() {
  transaction?.abort(); requestTransport?.dispose(); requestTransport = undefined
  uploader?.invalidate(); uploader = undefined; readPlan.value = undefined; confirmed.value = false; backupDownloaded.value = false
  if (!busy.value) progress.value = { phase: 'idle', sentSurfaces: 0, acknowledgedSurfaces: 0, stored: false, message: '' }
}
watch(() => JSON.stringify(props.template) + inputId.value + '|' + outputId.value + '|' + slot.value, invalidatePlan, { flush: 'sync' })
const stopState = session.onStateChange(() => {
  if (requestTransport && !requestTransport.connected) invalidatePlan()
  ports.value = session.listPorts(); connected.value = session.connected
  if (inputId.value && !session.inputId) { inputId.value = ''; capture.value = false; error.value = 'MIDI input disconnected. Select an input to resume.' }
  if (outputId.value && !ports.value.some(p => p.id === outputId.value && p.type === 'output' && p.state === 'connected')) { outputId.value = ''; error.value = 'MIDI output disconnected. No transfer was attempted.' }
})
const stopMessages = session.onMessage(m => {
  if (m.bytes.length <= 16384) latestMessage.value = m
  if (!capture.value || (excludeClock.value && m.bytes.length === 1 && m.bytes[0] === 0xf8)) return
  if (messages.value.length >= 500 || m.bytes.length > 16384) { capture.value = false; captureFull.value = true; return }
  messages.value.push(m)
})
// The model on the MIDI ports, by name (families.ts): one of this family that isn't the one drawn, the drawing follows;
// the ports of the model drawn (its main ones: not DAW, HUI, DIN) are chosen when nothing is yet. Nothing is sent.
const found = computed(() => detectModels(ports.value.filter(p => p.state === 'connected').map(p => p.name)))
watch(found, ids => {
  const mine = ids.filter(id => familyOf(id) === familyOf(props.template.deviceProfileId))
  if (props.compact && mine.length === 1 && mine[0] !== props.template.deviceProfileId) emit('use-model', mine[0]!)
})
watch([found, () => props.template.deviceProfileId, busy], () => {
  if (!props.compact || busy.value) return
  const own = (type: string) => ports.value.filter(p => p.type === type && p.state === 'connected' && detectModels([p.name])[0] === props.template.deviceProfileId)
  const ins = own('input'), outs = own('output')
  if (!inputId.value && ins.length === 1) { inputId.value = ins[0]!.id; selectInput() }
  if (!outputId.value && outs.length === 1) outputId.value = outs[0]!.id
})
const portsLine = computed(() => [inputs.value.find(p => p.id === inputId.value)?.name, outputs.value.find(p => p.id === outputId.value)?.name].filter((n, i, all) => n && all.indexOf(n) === i).join(' / '))
async function connect() {
  busy.value = true; error.value = ''
  try { await session.connect() } catch (e) { error.value = (e as Error).message }
  finally { busy.value = false }
}
function selectInput() {
  capture.value = false
  try { session.selectInput(inputId.value); error.value = '' } catch (e) { error.value = (e as Error).message }
}
function disconnect() { invalidatePlan(); capture.value = false; inputId.value = ''; outputId.value = ''; session.disconnect() }
async function verifyAndBackup() {
  if (props.template.deviceProfileId !== XL3_FILE_SERIALIZER.deviceProfileId) return
  busy.value = true; error.value = ''; invalidatePlan()
  try {
    const input = inputs.value.find(p => p.id === inputId.value), output = outputs.value.find(p => p.id === outputId.value)
    if (!input || !output || !isXL3MainPortName(input.name) || !isXL3MainPortName(output.name)) throw new Error('Choose the XL3 main MIDI input/output, excluding DAW, DIN and bootloader ports.')
    transactionTrace.value = []; traceTruncated.value = false
    traceContext.value = { inputId: inputId.value, outputId: outputId.value, slot: slot.value, modeName: props.template.name }
    traceProgress.value = undefined
    requestTransport = session.createRequestTransport(inputId.value, outputId.value, entry => {
      if (transactionTrace.value.length >= 256 || entry.bytes.length > 16384) { traceTruncated.value = true; return }
      transactionTrace.value.push(entry)
    })
    transaction = new AbortController()
    uploader = createXL3Uploader(requestTransport, state => { progress.value = state; traceProgress.value = state })
    readPlan.value = await uploader.prepare(engine.exportFile(JSON.parse(JSON.stringify(props.template)), XL3_FILE_SERIALIZER), slot.value, transaction.signal)
  } catch (e) { error.value = (e as Error).message }
  finally { busy.value = false }
}
function downloadTransaction() {
  const data = { schemaVersion: 1, ...traceContext.value, progress: traceProgress.value, truncated: traceTruncated.value, entries: transactionTrace.value }
  saveBlob(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }), 'xl3-slot1-transaction.json')
}
function downloadBackup() {
  if (!readPlan.value) return
  saveBlob(new Blob([new Uint8Array(readPlan.value.backup).buffer], { type: 'application/octet-stream' }), 'xl3-slot1-backup.syx')
  backupDownloaded.value = true
}
async function uploadPrepared() {
  if (!uploader || !readPlan.value || !confirmed.value || !backupDownloaded.value || !XL3_UPLOAD_EXPERIMENT_ENABLED) return
  busy.value = true; error.value = ''
  const token = readPlan.value.token; confirmed.value = false
  try { await uploader.upload(engine.exportFile(JSON.parse(JSON.stringify(props.template)), XL3_FILE_SERIALIZER), slot.value, token, transaction?.signal) }
  catch (e) { error.value = (e as Error).message }
  finally { busy.value = false; readPlan.value = undefined }
}
function saveBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob), a = document.createElement('a')
  a.href = url; a.download = filename; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000)
}
function clearCapture() { messages.value = []; captureFull.value = false }
function downloadCapture() {
  const blob = new Blob([JSON.stringify({ schemaVersion: 1, source: 'passive-web-midi-input', inputPorts: inputs.value, selectedInput: inputId.value, selectedOutput: outputId.value, identityVerified: false, excludeMidiClock: excludeClock.value, messages: messages.value }, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob), a = document.createElement('a')
  a.href = url; a.download = 'xl3-passive-capture.json'; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000)
}
onBeforeUnmount(() => { invalidatePlan(); stopState(); stopMessages(); session.dispose() })
</script>
<template>
  <section class="hardware-panel" aria-label="Hardware connection">
    <h2 v-if="!compact">Hardware connection</h2>
    <label v-if="compact && !isMk2" class="mode-selector">Custom mode<select v-model.number="slot" :disabled="busy || launchpadBusy || restoreAvailable" aria-label="Destination custom mode"><option v-for="n in deviceProfile.customModeSlots" :key="n" :value="n">Mode {{ n }}</option></select></label>
    <p v-if="!compact">Connect to enumerate MIDI ports and optionally capture incoming messages. Port names do not verify the controller model or firmware.</p>
    <div class="actions"><button v-if="!connected" :disabled="!support.supported || busy" @click="connect">{{ busy ? 'Requesting MIDI permission…' : 'Connect MIDI (SysEx permission)' }}</button><button v-else :disabled="busy || launchpadBusy" @click="disconnect">Disconnect MIDI</button><span>{{ connected ? 'MIDI access granted' : support.reason }}</span></div>
    <p v-if="error" role="alert" class="error">{{ error }}</p>
    <p v-if="compact && connected && inputId && outputId && !choosePorts" class="ports-line">Using <b>{{ portsLine }}</b> <button class="link" @click="choosePorts = true">change</button></p>
    <div v-if="connected && (!compact || choosePorts || !inputId || !outputId)" class="pair port-selectors">
      <label>MIDI input<select v-model="inputId" :disabled="busy || launchpadBusy" @change="selectInput"><option value="">Select input</option><option v-for="p in inputs" :key="p.id" :value="p.id" :disabled="p.state !== 'connected'">{{ p.name }} · {{ p.manufacturer }} · {{ p.state }}</option></select></label>
      <label>MIDI output<select v-model="outputId" :disabled="busy || launchpadBusy"><option value="">Select output</option><option v-for="p in outputs" :key="p.id" :value="p.id" :disabled="p.state !== 'connected'">{{ p.name }} · {{ p.manufacturer }} · {{ p.state }}</option></select></label>
    </div>
    <p v-if="connected && !ports.length">No MIDI ports found. Check the controller connection.</p>
    <details v-if="!compact"><summary>Passive input capture</summary><p>This receives only the selected input; it sends no queries or configuration. Captures may help document physical output or replies visible on this input. Browser MIDI access may conflict with Components; it cannot intercept Components’ outgoing traffic.</p><label><input v-model="excludeClock" type="checkbox" :disabled="capture || messages.length > 0"> Exclude MIDI clock from capture (clear capture before changing)</label><div class="actions"><button :disabled="!inputId || captureFull" @click="capture = !capture">{{ capture ? 'Stop capture' : 'Start capture' }}</button><button @click="clearCapture">Clear capture</button><button :disabled="!messages.length" @click="downloadCapture">Download capture JSON</button></div><p role="status">{{ messages.length }} captured messages{{ capture ? ' · Listening' : '' }}{{ captureFull ? ' · Capture limit reached; clear to resume.' : '' }}</p><pre v-if="messages.length" class="capture-preview">{{ messages.slice(-5).map(m => `${m.inputId} ${m.timestamp.toFixed(1)}: ${m.bytes.map(b => b.toString(16).padStart(2, '0')).join(' ')}`).join('\n') }}</pre></details>
    <MidiMonitor v-if="!compact" :template="template" :incoming="latestMessage" :input-name="inputs.find(p => p.id === inputId)?.name" />
    <SlotSend v-if="compact && !isMk2" :template="template" :slot="slot" :launchpad="template.deviceProfileId === LAUNCHPAD_PRO_MK3_PROFILE.id" :ready="connected && !!inputId && !!outputId" :create-transport="() => session.createRequestTransport(inputId, outputId)" @busy="busy = $event" />
    <template v-else-if="template.deviceProfileId === XL3_FILE_SERIALIZER.deviceProfileId">
    <h3>Prepare transfer</h3><label v-if="!compact" class="slot-field">Destination custom-mode slot<input v-model.number="slot" :disabled="busy" type="number" min="1" max="15"></label>
    <p>Uploading would overwrite both surfaces of custom-mode slot {{ slot }}. Direct transfer currently supports slot 1, firmware build 1563 and absolute CC mappings. Close Components before using this connection to avoid competing requests.</p>
    <p v-if="preparation.error" class="error" role="alert">{{ preparation.error }}</p><p v-else>File validated: {{ preparation.value?.name }} · {{ preparation.value?.byteLength }} bytes · 2 surfaces.</p>
    <div class="actions"><button :disabled="busy || !connected || !inputId || !outputId || slot !== 1 || !!preparation.error" @click="verifyAndBackup">Verify device &amp; back up slot 1</button><button :disabled="!readPlan || busy" @click="downloadBackup">Download slot 1 backup</button><button :disabled="!transactionTrace.length || busy" @click="downloadTransaction">Download transaction log</button><button v-if="busy && transaction" @click="transaction.abort()">Cancel operation</button></div>
    <p v-if="progress.message" role="status">{{ progress.message }}</p>
    <p v-if="readPlan">XL3 application identity verified · firmware build {{ readPlan.identity.firmwareBuild }} · current slot: {{ readPlan.backupName }}. No write performed.</p>
    <p><strong>{{ XL3_UPLOAD_LIMIT }}</strong></p>
    <label v-if="readPlan" class="overwrite-confirmation"><input v-model="confirmed" type="checkbox" :disabled="busy || !XL3_UPLOAD_EXPERIMENT_ENABLED"> Overwrite slot 1 with {{ template.name }} on the selected XL3. I have downloaded its backup.</label>
    <button :disabled="!XL3_UPLOAD_EXPERIMENT_ENABLED || !readPlan || !confirmed || !backupDownloaded || busy" @click="uploadPrepared">{{ XL3_UPLOAD_EXPERIMENT_ENABLED ? 'Confirm overwrite & upload' : 'Confirm overwrite & upload (unavailable)' }}</button>
    <p>Browser accepted {{ progress.sentSurfaces }}/2 surface sends · Matching replies {{ progress.acknowledgedSurfaces }}/2 · Read-back {{ progress.stored ? 'matches' : 'not verified for this operation' }} · Physical output requires a separate check.</p>
    <p>Use Download .syx above for the verified Components file-export path. Changing the device, mapping or slot invalidates the preparation and confirmation.</p>
    </template>
    <Mk2Send v-else-if="isMk2" :template="template" :ready="connected && !!inputId && !!outputId" :create-transport="() => session.createRequestTransport(inputId, outputId)" @busy="busy = $event" @destination-slot="emit('destination-slot', $event)" />
    <p v-else-if="slot !== 1" role="status">This app cannot send to the selected custom mode yet. Choose mode 1 here, or open the downloaded .syx file in Novation’s Components app and send it to this mode there.</p>
    <LaunchpadTransfer v-else :destination-slot="slot" @busy="launchpadBusy = $event" @restore-available="restoreAvailable = $event" :template="template" :input="inputs.find(p => p.id === inputId)" :output="outputs.find(p => p.id === outputId)" :create-transport="onTrace => session.createRequestTransport(inputId, outputId, onTrace)" />
    <details v-if="compact && inputId" class="troubleshoot"><summary>Troubleshooting: what the controller sends</summary><ControllerCheck :template="template" :incoming="latestMessage" /></details>
  </section>
</template>
<style scoped>
.ports-line{margin:6px 0}
.troubleshoot{margin-top:16px}
.troubleshoot summary{cursor:pointer;font-size:13px;opacity:.8}
.link{border:0;background:none;padding:0;text-decoration:underline;cursor:pointer;color:inherit;opacity:.8}
</style>
