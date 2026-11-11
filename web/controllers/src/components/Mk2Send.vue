<script setup lang="ts">
/** Sending a template to the Launch Control XL mk2: its identity first (family 61 00, read-only), then, once
 * confirmed, the user template file (the bytes Components exports: research/hardware/lcxl-mk2) and the template
 * select from Novation's programmer's reference (F0 00 20 29 02 11 77 tt F7), so the check below can follow at once.
 * The mk2 can't send a template back: there is no backup of the one replaced. */
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import type { ControllerTemplate } from '../core/mapping-engine/types'
import { useMapperContext } from '../integration/context'
import { LCXL_MK2_FILE_SERIALIZER } from '../devices/novation/launch-control-xl-mk2/adapter'
import { LCXL_MK2_PROFILE } from '../devices/novation/launch-control-xl-mk2/profile'
import { IDENTITY_REQUEST, identifyModel, isIdentityReply } from '../devices/families'
import type { MidiRequestTransport } from '../midi/request-transport'
const props = defineProps<{ template: ControllerTemplate; ready: boolean; createTransport: () => MidiRequestTransport }>()
const emit = defineEmits<{ busy: [value: boolean]; 'destination-slot': [value: number] }>()
const { engine } = useMapperContext()
const slot = ref(props.template.destinationSlot ?? 1), confirmed = ref(false), busy = ref(false), error = ref(''), sent = ref('')
watch(slot, v => { emit('destination-slot', v); confirmed.value = false })
watch(() => props.template.destinationSlot, v => { slot.value = v ?? 1 })
watch(() => JSON.stringify(props.template.assignments), () => { confirmed.value = false; sent.value = '' })
watch(busy, v => emit('busy', v), { flush: 'sync' })
const file = computed(() => { try { return { bytes: engine.exportFile({ ...JSON.parse(JSON.stringify(props.template)), destinationSlot: slot.value }, LCXL_MK2_FILE_SERIALIZER), error: '' } } catch (e) { return { bytes: undefined, error: (e as Error).message } } })
let transport: MidiRequestTransport | undefined
async function send() {
  if (!confirmed.value || !file.value.bytes || busy.value) return
  busy.value = true; error.value = ''; sent.value = ''
  try {
    transport = props.createTransport()
    const reply = await transport.exchange(IDENTITY_REQUEST, isIdentityReply)
    if (identifyModel(reply) !== LCXL_MK2_PROFILE.id) throw new Error('That port answers as another controller, not a Launch Control XL mk2. Nothing was sent.')
    transport.send!(file.value.bytes)
    transport.send!(Uint8Array.of(0xf0, 0, 0x20, 0x29, 2, 0x11, 0x77, slot.value - 1, 0xf7))
    sent.value = `Sent to user template ${slot.value}, and selected.`
  } catch (e) { error.value = (e as Error).message }
  finally { transport?.dispose(); transport = undefined; busy.value = false; confirmed.value = false }
}
onBeforeUnmount(() => transport?.dispose())
</script>
<template>
  <section class="mk2-send" aria-label="Send to the controller">
    <label class="mode-selector">User template<select v-model.number="slot" :disabled="busy" aria-label="Destination user template"><option v-for="n in 8" :key="n" :value="n">User {{ n }}</option></select></label>
    <p v-if="file.error" class="error" role="alert">{{ file.error }}</p>
    <label class="overwrite-confirmation"><input v-model="confirmed" type="checkbox" :disabled="busy || !ready || !!file.error"> Replace user template {{ slot }} on the controller. (The mk2 can't send a template back, so the one there now isn't kept.)</label>
    <div class="actions"><button class="mk2-send-button" :disabled="!confirmed || busy || !ready || !!file.error" @click="send">{{ busy ? 'Sending…' : 'Send' }}</button></div>
    <p v-if="error" class="error" role="alert">{{ error }}</p>
    <p v-if="sent" role="status">{{ sent }}</p>
  </section>
</template>
<style scoped>
.mk2-send{display:grid;gap:8px;margin-top:10px}
</style>
