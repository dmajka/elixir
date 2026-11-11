<script setup lang="ts">
import { onBeforeUnmount, ref, shallowRef, watch } from 'vue'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import { useMapperContext } from '../integration/context'
const { engine } = useMapperContext()
import { LAUNCHPAD_PRO_MK3_FILE_SERIALIZER } from '../devices/novation/launchpad-pro-mk3/adapter'
import { decodeLaunchpadFile } from '../devices/novation/launchpad-pro-mk3/codec'
import { createLaunchpadUploader, MK3_BROWSER_WRITE_ENABLED, type LaunchpadUploadProgress } from '../devices/novation/launchpad-pro-mk3/upload'
import type { MidiPort } from '../midi/provider'
import type { MidiRequestTransport, MidiTransactionTrace } from '../midi/request-transport'
import { isLaunchpadMainPort, readLaunchpadSlot1 } from '../devices/novation/launchpad-pro-mk3/hardware-protocol'

const props = defineProps<{
  destinationSlot?: number
  template?: ControllerTemplate
  input?: MidiPort
  output?: MidiPort
  createTransport: (onTrace: (entry: MidiTransactionTrace) => void) => MidiRequestTransport
}>()
const emit = defineEmits<{ busy: [value: boolean]; 'restore-available': [value: boolean] }>()
const busy = ref(false), error = ref(''), truncated = ref(false)
const result = shallowRef<Awaited<ReturnType<typeof readLaunchpadSlot1>>>()
const trace = shallowRef<MidiTransactionTrace[]>([])
const tracePorts = shallowRef<{ input?: MidiPort; output?: MidiPort }>({})
const plan = shallowRef<Awaited<ReturnType<ReturnType<typeof createLaunchpadUploader>['prepare']>>>()
const confirmed = ref(false), backupDownloaded = ref(false), canRestore = ref(false)
const progress = shallowRef<LaunchpadUploadProgress>()
watch(busy, value => emit('busy', value), { flush: 'sync' })
watch(canRestore, value => emit('restore-available', value), { flush: 'sync' })
watch(() => props.destinationSlot, invalidate)
let uploader: ReturnType<typeof createLaunchpadUploader> | undefined
let transport: MidiRequestTransport | undefined
let operation: AbortController | undefined
function invalidate() {
  operation?.abort()
  transport?.dispose()
  result.value = undefined
  plan.value = undefined
  confirmed.value = false
  backupDownloaded.value = false
  canRestore.value = false
  uploader?.invalidate()
}
watch(() => JSON.stringify([props.input?.id, props.input?.state, props.output?.id, props.output?.state]), invalidate)
watch(() => JSON.stringify(props.template), () => {
  if (canRestore.value) { plan.value = undefined; confirmed.value = false; return }
  if (busy.value) { operation?.abort(); plan.value = undefined; confirmed.value = false; return }
  invalidate()
})
onBeforeUnmount(invalidate)
async function verify() {
  invalidate()
  error.value = ''
  trace.value = []
  tracePorts.value = { input: props.input ? { ...props.input } : undefined, output: props.output ? { ...props.output } : undefined }
  truncated.value = false
  busy.value = true
  try {
    if ((props.destinationSlot ?? 1) !== 1) throw new Error('Only MK3 custom mode 1 is verified for direct transfer')
    if (!props.input || !props.output || !isLaunchpadMainPort(props.input.name) || !isLaunchpadMainPort(props.output.name)) {
      throw new Error('Select the Launchpad Pro MK3 main MIDI input and output, excluding DAW and DIN.')
    }
    operation = new AbortController()
    transport = props.createTransport(entry => {
      // Continuous MIDI clock must not exhaust the transaction evidence.
      if (entry.bytes[0] !== 0xf0) return
      if (trace.value.length >= 32 || entry.bytes.length > 16384) { truncated.value = true; return }
      trace.value = [...trace.value, entry]
    })
    result.value = await readLaunchpadSlot1(transport, operation.signal)
  } catch (e) {
    error.value = (e as Error).message
  } finally {
    transport?.dispose()
    busy.value = false
  }
}
function candidateFile() {
  if (!props.template) throw new Error('Load an MK3 mapping before preparing a write')
  return engine.exportFile(JSON.parse(JSON.stringify(props.template)), LAUNCHPAD_PRO_MK3_FILE_SERIALIZER)
}
async function prepareWrite() {
  invalidate()
  error.value = ''
  busy.value = true
  trace.value = []
  truncated.value = false
  tracePorts.value = { input: props.input ? { ...props.input } : undefined, output: props.output ? { ...props.output } : undefined }
  try {
    if ((props.destinationSlot ?? 1) !== 1) throw new Error('Only MK3 custom mode 1 is verified for direct transfer')
    if (!props.input || !props.output || !isLaunchpadMainPort(props.input.name) || !isLaunchpadMainPort(props.output.name)) throw new Error('Select the MK3 main MIDI input and output')
    operation = new AbortController()
    transport = props.createTransport(entry => {
      if (entry.bytes[0] !== 0xf0) return
      if (trace.value.length >= 64 || entry.bytes.length > 16384) { truncated.value = true; return }
      trace.value = [...trace.value, entry]
    })
    uploader = createLaunchpadUploader(transport, state => { progress.value = state })
    plan.value = await uploader.prepare(candidateFile(), operation.signal)
    result.value = { identity: plan.value.identity, backup: plan.value.backup, mode: decodeLaunchpadFile(plan.value.backup) }
  } catch (e) { error.value = (e as Error).message }
  finally { busy.value = false }
}
async function writeTest() {
  if ((props.destinationSlot ?? 1) !== 1) return
  if (!uploader || !plan.value || !confirmed.value || !backupDownloaded.value || !MK3_BROWSER_WRITE_ENABLED) return
  const token = plan.value.token
  plan.value = undefined
  confirmed.value = false
  busy.value = true
  error.value = ''
  try {
    await uploader.upload(candidateFile(), token, operation?.signal)
    canRestore.value = true
  } catch (e) {
    error.value = (e as Error).message
    canRestore.value = true
  } finally { busy.value = false }
}
async function restoreOriginal() {
  if (!uploader || !canRestore.value || !MK3_BROWSER_WRITE_ENABLED) return
  busy.value = true
  error.value = ''
  try {
    // A cancellation token from the test must not cancel the authorized restoration.
    operation = new AbortController()
    await uploader.restore(operation.signal)
    canRestore.value = false
  } catch (e) { error.value = (e as Error).message }
  finally { busy.value = false }
}
function download(kind: 'backup' | 'trace') {
  const bytes = kind === 'backup' ? result.value?.backup : undefined
  if (kind === 'backup' && bytes) backupDownloaded.value = true
  if (kind === 'backup' && !bytes) return
  const data = bytes ? new Uint8Array(bytes).buffer : JSON.stringify({
    schemaVersion: 1, ...tracePorts.value, truncated: truncated.value,
    identity: result.value?.identity, progress: progress.value, entries: trace.value,
  }, null, 2)
  const url = URL.createObjectURL(new Blob([data], { type: bytes ? 'application/octet-stream' : 'application/json' }))
  const anchor = document.createElement('a')
  anchor.href = url
  anchor.download = kind === 'backup' ? 'launchpad-slot1-browser-backup.syx' : 'launchpad-slot1-read-transaction.json'
  anchor.click()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
</script>
<template>
  <section aria-label="Launchpad Pro MK3 transfer">
    <h3>Launchpad Pro MK3 backup &amp; upload</h3>
    <p>Close Components, select the main MIDI input and output, then verify the device and retrieve custom mode 1.</p>
    <div class="actions">
      <button :disabled="busy || !input || !output || canRestore" @click="verify">Verify MK3 &amp; back up slot 1</button>
      <button :disabled="busy || !result" @click="download('backup')">Download MK3 slot 1 backup</button>
      <button :disabled="busy || !trace.length" @click="download('trace')">Download MK3 transaction log</button>
      <button v-if="busy" @click="operation?.abort()">Cancel MK3 operation</button>
    </div>
    <p v-if="busy" role="status">Reading device identity and custom mode 1…</p>
    <p v-if="error" role="alert" class="error">{{ error }}</p>
    <p v-if="result" role="status">MK3 identity verified · firmware build {{ result.identity.firmwareBuild }} · backed up slot 1 {{ result.mode.name }} · {{ result.backup.length }} bytes.</p>
    <button :disabled="busy || !input || !output || canRestore" @click="prepareWrite">Prepare MK3 slot 1 upload</button>
    <p v-if="plan">Prepared {{ plan.modeName }} for slot 1. Upload replaces {{ plan.backupName }}. Download the fresh backup, then confirm.</p>
    <label v-if="plan"><input v-model="confirmed" type="checkbox" :disabled="busy || !MK3_BROWSER_WRITE_ENABLED"> Overwrite MK3 slot 1 with {{ plan.modeName }} .</label>
    <button :disabled="busy || !plan || !confirmed || !backupDownloaded || !MK3_BROWSER_WRITE_ENABLED" @click="writeTest">Confirm MK3 overwrite &amp; upload</button>
    <button v-if="canRestore" :disabled="busy || !MK3_BROWSER_WRITE_ENABLED" @click="restoreOriginal">Restore original MK3 slot 1</button>
    <p v-if="progress" role="status">{{ progress.message }} · Browser accepted {{ progress.acceptedSends }} mode sends · Stored test {{ progress.stored ? 'matches' : 'pending' }} · Original {{ progress.restored ? 'restored' : 'awaiting restoration' }}</p>
    <p>Direct upload supports slot 1, firmware build 483 and momentary CC and note pads on explicit channels. To select custom mode 1, quickly tap and release Custom, then quickly tap and release the leftmost Track Select button immediately below the grid. Leave both buttons released while using the pads.</p>
  </section>
</template>
