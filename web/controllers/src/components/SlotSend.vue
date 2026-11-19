<script setup lang="ts">
/** 1.4.4: sending a template straight to any custom mode of a Launch Control XL3 (1-15) or Launchpad Pro MK3 (1-8).
 * One press: the device's identity, a read of the mode (kept: it can be downloaded afterwards), the write, and a
 * read-back that must match. A backup beforehand is offered, never required. The read comes first, so a mode whose
 * reply doesn't answer as expected fails before anything is written. */
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import { useMapperContext } from '../integration/context'
import { XL3_FILE_SERIALIZER } from '../devices/novation/launch-control-xl3/adapter'
import { LAUNCHPAD_PRO_MK3_FILE_SERIALIZER } from '../devices/novation/launchpad-pro-mk3/adapter'
import { createXL3Uploader } from '../devices/novation/launch-control-xl3/upload'
import { createLaunchpadUploader } from '../devices/novation/launchpad-pro-mk3/upload'
import type { MidiRequestTransport } from '../midi/request-transport'
const props = defineProps<{ template: ControllerTemplate; slot: number; ready: boolean; launchpad: boolean; createTransport: () => MidiRequestTransport }>()
const emit = defineEmits<{ busy: [value: boolean] }>()
const { engine } = useMapperContext()
const busy = ref(false), error = ref(''), status = ref('')
const previous = ref<{ bytes: Uint8Array; name: string; slot: number }>()
watch(busy, v => emit('busy', v), { flush: 'sync' })
watch(() => props.slot + JSON.stringify(props.template), () => { error.value = ''; status.value = '' })
const file = computed(() => {
  try { return { bytes: engine.exportFile(JSON.parse(JSON.stringify(props.template)), props.launchpad ? LAUNCHPAD_PRO_MK3_FILE_SERIALIZER : XL3_FILE_SERIALIZER), error: '' } }
  catch (e) { return { bytes: undefined, error: (e as Error).message } }
})
let transport: MidiRequestTransport | undefined
async function withDevice(work: (t: MidiRequestTransport) => Promise<void>) {
  if (busy.value || !file.value.bytes) return
  busy.value = true; error.value = ''; status.value = ''
  try { transport = props.createTransport(); await work(transport) }
  catch (e) { error.value = (e as Error).message }
  finally { transport?.dispose(); transport = undefined; busy.value = false }
}
async function read(t: MidiRequestTransport, bytes: Uint8Array, slot: number) {
  try { return await readOnly(t, bytes, slot) }
  catch (e) { throw new Error(`Couldn't read mode ${slot} first, so nothing was written. (${(e as Error).message.split('.')[0]}.)`) }
}
async function readOnly(t: MidiRequestTransport, bytes: Uint8Array, slot: number) {
  if (props.launchpad) {
    const u = createLaunchpadUploader(t), plan = await u.prepare(bytes, undefined, slot)
    return { plan, write: () => u.upload(bytes, plan.token) }
  }
  const u = createXL3Uploader(t), plan = await u.prepare(bytes, slot)
  return { plan, write: () => u.upload(bytes, slot, plan.token).then(() => {}) }
}
const send = () => withDevice(async t => {
  const slot = props.slot, { plan, write } = await read(t, file.value.bytes!, slot)
  previous.value = { bytes: plan.backup.slice(), name: plan.backupName, slot }
  status.value = `Sending to mode ${slot}…`
  await write()
  status.value = props.launchpad
    ? `Sent to custom mode ${slot}. On the Launchpad: tap Custom, then Track Select button ${slot} below the grid.`
    : `Sent to custom mode ${slot}.`
})
const backup = () => withDevice(async t => {
  const slot = props.slot, { plan } = await read(t, file.value.bytes!, slot)
  previous.value = { bytes: plan.backup.slice(), name: plan.backupName, slot }
  save(previous.value)
  status.value = `Backed up mode ${slot} (${plan.backupName}).`
})
function save(p: { bytes: Uint8Array; name: string; slot: number }) {
  const url = URL.createObjectURL(new Blob([new Uint8Array(p.bytes).buffer], { type: 'application/octet-stream' })), a = document.createElement('a')
  a.href = url; a.download = `${(p.name || `mode-${p.slot}`).replace(/[^\w.-]+/g, '-')}-backup.syx`; a.click(); setTimeout(() => URL.revokeObjectURL(url), 1000)
}
onBeforeUnmount(() => transport?.dispose())
</script>
<template>
  <section class="slot-send" aria-label="Send to the controller">
    <p v-if="file.error" class="error" role="alert">{{ file.error }}</p>
    <div class="actions">
      <button class="slot-send-button" :disabled="busy || !ready || !!file.error" @click="send">{{ busy ? 'Working…' : `Send to mode ${slot}` }}</button>
      <button :disabled="busy || !ready || !!file.error" @click="backup">Back up mode {{ slot }} first</button>
    </div>
    <p class="hint">Sending replaces what's in mode {{ slot }}. Close Novation Components first.</p>
    <p v-if="error" class="error" role="alert">{{ error }}</p>
    <p v-if="status" role="status">{{ status }}</p>
    <p v-if="previous && status.startsWith('Sent')" class="hint">Changed your mind? <button class="link" @click="save(previous)">Download what was in mode {{ previous.slot }}</button> ({{ previous.name }}) to restore it with Components.</p>
  </section>
</template>
<style scoped>
.slot-send{display:grid;gap:8px;margin-top:10px}
.hint{opacity:.8;margin:0}
.link{border:0;background:none;padding:0;text-decoration:underline;cursor:pointer;color:inherit}
</style>
