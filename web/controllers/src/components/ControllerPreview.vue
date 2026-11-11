<script setup lang="ts">
import { computed, ref } from 'vue'
import { LED_COLORS, PARAMETER_DRAG_TYPE } from './led-colors'
import type { DeviceProfile, MidiAssignment } from '../core/mapping-engine/types'
import { useMapperContext } from '../integration/context'
/** selection: the controls selected together (the compact view); without it, only `selected` is shown. */
const props = defineProps<{ profile: DeviceProfile; assignments: MidiAssignment[]; selected: string; selection?: readonly string[] }>()
const emit = defineEmits<{ select: [id: string]; 'select-many': [ids: string[], focus: string]; 'drop-parameter': [controlId: string, parameterId: string] }>()
const isSelected = (id: string) => props.selection ? props.selection.includes(id) : props.selected === id

// Several at once, as Novation Components does: Shift-click a range from the last control, Cmd/Ctrl-click one in or
// out, or drag across the controls for the rectangle between (Cmd/Ctrl held: added to what was selected).
function rectangle(from: string, to: string) {
  const a = props.profile.controls.find(c => c.id === from), b = props.profile.controls.find(c => c.id === to)
  if (!a || !b) return [to]
  const [r0, r1] = [Math.min(a.row, b.row), Math.max(a.row, b.row)], [c0, c1] = [Math.min(a.column, b.column), Math.max(a.column, b.column)]
  return props.profile.controls.filter(c => c.row >= r0 && c.row <= r1 && c.column >= c0 && c.column <= c1).map(c => c.id)
}
const union = (a: readonly string[], b: readonly string[]) => [...new Set([...a, ...b])]
let anchor = '', base: readonly string[] = [], adding = false, dragged = false
function down(event: PointerEvent, id: string) {
  if (!props.selection || event.button !== 0) return
  anchor = id; base = props.selection; adding = event.metaKey || event.ctrlKey; dragged = false
}
function move(event: PointerEvent) {
  if (!anchor || !(event.buttons & 1)) return
  const id = (document.elementFromPoint(event.clientX, event.clientY)?.closest('[data-control]') as HTMLElement | null)?.dataset.control
  if (!id || (id === anchor && !dragged)) return
  dragged = true
  const ids = rectangle(anchor, id)
  emit('select-many', adding ? union(base, ids) : ids, id)
}
function up() { anchor = '' }
function click(event: MouseEvent, id: string) {
  if (dragged) { dragged = false; return }                 // (the end of a drag, not a click)
  const adding = event.metaKey || event.ctrlKey
  if (props.selection && event.shiftKey) emit('select-many', union(adding ? props.selection : [], rectangle(props.selected, id)), id)
  else if (props.selection && adding) {
    const ids = props.selection.includes(id) ? props.selection.filter(x => x !== id) : [...props.selection, id]
    if (ids.length) emit('select-many', ids, ids.includes(id) ? id : ids.at(-1)!)
  } else emit('select', id)
}
const dropTarget = ref('')
function drop(event: DragEvent, id: string) {
  dropTarget.value = ''
  const parameterId = event.dataTransfer?.getData(PARAMETER_DRAG_TYPE)
  if (parameterId) emit('drop-parameter', id, parameterId)
}
const { parameters } = useMapperContext()
const launchpad = computed(() => props.profile.id === 'novation.launchpad-pro-mk3')
const mk2 = computed(() => props.profile.id === 'novation.launch-control-xl-mk2')
function assignment(id: string) { return props.assignments.find(a => a.controlId === id) }
function label(id: string) {
  const a = assignment(id)
  return a ? a.label || parameters.find(p => p.id === a.parameterId)?.shortName || a.parameterId : 'Unassigned'
}
// Approximate the supported Novation palette for workspace identification, not live LED feedback.
function color(id: string, row: number) {
  const a = assignment(id)
  return a ? LED_COLORS[a.colors?.off ?? -1]?.hex ?? ['#70dcb2', '#66cce1', '#7ca5ed'][row % 3] : '#45494e'
}
const top = ['◀', '▶', 'Session', 'Note', 'Chord', 'Custom', 'Sequencer', 'Projects']
const left = ['▲', '▼', 'Clear', 'Duplicate', 'Loop length', 'Quantise', 'Fixed length', 'Record']
const right = ['Patterns', 'Steps', 'Pattern settings', 'Velocity', 'Probability', 'Mutation', 'Micro step', 'Print to clip']
const bottom = ['Record arm', 'Mute', 'Solo', 'Volume', 'Pan', 'Sends', 'Device', 'Stop clip']
</script>
<template>
  <div class="device-scroll" tabindex="0" aria-label="Controller preview; scroll horizontally on small screens">
    <div :class="['device-shell', launchpad ? 'device-launchpad' : 'device-xl3', { 'device-mk2': mk2 }]">
      <div class="device-brand" aria-hidden="true"><strong>{{ launchpad ? 'LAUNCHPAD' : 'LAUNCH CONTROL' }}<span>{{ launchpad ? ' PRO' : mk2 ? ' XL' : ' XL 3' }}</span></strong><span class="device-maker">novation ◇</span></div>
      <div class="device-face">
        <template v-if="launchpad">
          <div class="pad-top device-decoration" aria-hidden="true"><span v-for="name in top" :key="name" :class="{ active: name === 'Custom' }">{{ name }}</span></div>
          <div class="pad-left device-decoration" aria-hidden="true"><span v-for="name in left" :key="name">{{ name }}</span></div>
          <div class="pad-right device-decoration" aria-hidden="true"><span v-for="name in right" :key="name">›<small>{{ name }}</small></span></div>
          <div class="pad-bottom device-decoration" aria-hidden="true"><div class="track-select"><span v-for="n in 8" :key="n">{{ n }}</span></div><div class="track-functions"><span v-for="name in bottom" :key="name">{{ name }}</span></div></div>
        </template>
        <aside v-else-if="mk2" class="xl-side mk2-side device-decoration" aria-hidden="true"><div>Send<span class="side-pair">▲　▼</span></div><div>Track<span class="side-pair">◀　▶</span></div><div class="mk2-modes"><span class="side-pair">Device</span><span class="side-pair">Mute</span><span class="side-pair">Solo</span><span class="side-pair">Record<br>Arm</span></div></aside>
        <aside v-else class="xl-side device-decoration" aria-hidden="true"><div class="device-display"><small>CUSTOM MODE</small><b>Elixir</b></div><div>Page<span class="side-pair">⌃　⌄</span></div><div>Track<span class="side-pair">‹　›</span><small>└ Settings ┘</small></div><span class="side-pair transport">●<br>▶</span><span class="side-pair">Shift<br>Mode</span></aside>
        <div :class="['controller', { multi: !!selection }]" aria-label="Controller preview" @pointermove="move" @pointerup="up" @pointercancel="up" @pointerleave="up">
          <button v-for="c in profile.controls" :key="c.id" :style="{ gridRow: c.row + 1, gridColumn: c.column + 1, '--control-color': color(c.id, c.row) }" :class="['control', c.kind, { assigned: !!assignment(c.id), 'drop-target': dropTarget === c.id }]" :data-control="c.id" :aria-pressed="isSelected(c.id)" :aria-label="`${c.name}: ${label(c.id)}`" :title="`${c.name}: ${label(c.id)}`" @pointerdown="down($event, c.id)" @click="click($event, c.id)" @dragover.prevent="dropTarget = c.id" @dragleave="dropTarget = ''" @drop.prevent="drop($event, c.id)">
            <span class="control-hardware" aria-hidden="true"><i v-if="c.kind === 'encoder'" class="knob"/><i v-else-if="c.kind === 'fader'" class="fader-track"><i class="fader-cap"/></i><i v-else class="pad-surface"/></span>
            <span class="control-label">{{ launchpad && !assignment(c.id) ? '' : label(c.id) }}</span>
          </button>
        </div>
      </div>
      <div class="device-caption">{{ launchpad ? 'PRO MK3 · CUSTOM MODE' : mk2 ? 'MK2 · USER TEMPLATE · 24 KNOBS / 8 FADERS' : 'CUSTOM MODE · 24 ENCODERS / 8 FADERS' }}<span>Click a {{ launchpad ? 'pad' : 'control' }} to assign</span></div>
    </div>
  </div>
  <p class="preview-note">{{ launchpad ? 'Surrounding buttons are shown for orientation; only the 64 pads are editable.' : 'Side controls are shown for orientation; the 48 main controls are editable.' }} Colors preview assignments, not live device feedback.</p>
</template>
<style scoped>
.device-scroll{overflow-x:auto;padding:8px 4px 20px;max-width:100%}.device-shell{--case:#242629;background:linear-gradient(125deg,#303236,#202225 65%,#292b2f);border:1px solid #4b4e52;border-bottom:7px solid #111214;border-radius:14px;padding:24px 22px 14px;box-shadow:0 12px 22px #0005,inset 0 1px 0 #ffffff15;color:#d6d9dd;min-width:550px;max-width:850px;margin:auto;container-type:inline-size}.device-brand{display:flex;align-items:center;justify-content:space-between;margin:0 0 24px;font-size:15px;letter-spacing:-.6px}.device-brand strong{font-weight:750}.device-brand strong span{font-weight:350;font-size:.8em}.device-maker{font-size:12px;letter-spacing:.4px;color:#a1a5ab}.device-face{display:grid;gap:12px}.device-xl3 .device-face{grid-template-columns:64px minmax(0,1fr)}.device-xl3 .controller{grid-template-rows:repeat(3,83px) 190px repeat(2,36px);gap:10px 9px}.device-shell .controller{display:grid;grid-template-columns:repeat(8,minmax(0,1fr))}.device-shell .controller .control{position:relative;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:0;min-height:0;min-width:0;padding:0;border:1px solid transparent;border-radius:5px;background:transparent;box-shadow:none;color:#c8cdd3;overflow:visible;font-size:9px;transition:background .15s,border-color .15s}.device-shell .controller.multi{touch-action:none;user-select:none;-webkit-user-select:none}.device-shell .controller .control.drop-target{outline:3px dashed #8af1c5;outline-offset:2px}.device-shell .controller .control:hover{background:#ffffff09;border-color:#8f969e}.device-shell .controller .control[aria-pressed=true]{background:#ffffff0b;border:1px solid #f2f5f7;box-shadow:0 0 0 2px #f2f5f721}.device-shell .controller .control:focus-visible{outline:3px solid #f4d284;outline-offset:3px}.control-hardware{position:relative;display:flex;align-items:center;justify-content:center;width:100%;height:100%;pointer-events:none}.control-label{width:100%;padding:3px 2px;text-align:center;line-height:1.15;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:9px;letter-spacing:-.15px}.knob{display:block;position:relative;width:34px;height:34px;border-radius:50%;background:radial-gradient(circle at 35% 25%,#55595c,#323639 65%);border:2px solid #62666a;box-shadow:2px 4px 5px #0009,inset 0 1px 2px #ffffff35}.knob:before{content:'';position:absolute;width:2px;height:10px;background:#bdc2c5;top:3px;left:14px;border-radius:1px;transform:rotate(28deg);transform-origin:1px 13px}.knob:after{content:'';position:absolute;width:10px;height:4px;top:42px;left:10px;background:var(--control-color);box-shadow:0 0 6px color-mix(in srgb,var(--control-color) 35%,transparent);border-radius:1px}.encoder .control-label{padding-top:11px}.fader-track{position:relative;height:145px;width:7px;background:#101113;border:1px solid #494c4f;border-radius:5px;box-shadow:inset 1px 0 2px #000}.fader-track:before{content:'';position:absolute;left:17px;top:6px;width:10px;height:130px;background:repeating-linear-gradient(to bottom,#86898c 0 1px,transparent 1px 16px);opacity:.6}.fader-cap{position:absolute;top:72px;left:-12px;width:29px;height:20px;border-radius:2px;border:1px solid #141517;background:linear-gradient(#54575a,#26282a 40%,#161719 48%,#e4e6e8 49%,#e4e6e8 59%,#303235 60%,#202224);box-shadow:1px 3px 4px #000a}.device-shell .controller .button .pad-surface{height:18px;width:80%;border-radius:3px;background:var(--control-color);opacity:.85;border:1px solid #ffffff25;box-shadow:0 2px 3px #0007}.device-shell .controller .button .control-label{font-size:8px;padding:1px}.xl-side{display:flex;flex-direction:column;gap:23px;font-size:9px;text-align:center;color:#989ea5}.device-display{border:5px solid #151719;border-radius:5px;background:#42494e;padding:8px 1px;box-shadow:0 0 0 1px #5b6065;line-height:1.4}.device-display small{font-size:6px;display:block;color:#c4cdd3}.device-display b{font-size:12px;color:#e1e7eb;font-weight:400}.side-pair{display:block;background:linear-gradient(#323538,#24272a);border:1px solid #111;box-shadow:0 1px 0 #ffffff18;border-radius:4px;padding:6px 0;font-size:14px;line-height:1.7;margin:3px 0}.xl-side small{font-size:8px}.transport{margin-top:14px;color:#9dd7a8;font-size:18px;line-height:2}.device-mk2 .device-face{grid-template-columns:minmax(0,1fr) 64px}.device-mk2 .xl-side{order:2}.mk2-modes{margin-top:auto;display:flex;flex-direction:column;gap:6px}.mk2-modes .side-pair{font-size:9px;line-height:1.3;padding:5px 0}.device-caption{display:flex;justify-content:space-between;font-size:8px;letter-spacing:1.1px;color:#92979f;margin-top:20px}.device-caption span{letter-spacing:0}.device-launchpad{max-width:720px;padding:22px 22px 16px}.device-launchpad .device-face{grid-template-columns:42px minmax(0,1fr) 42px;grid-template-rows:40px auto auto;gap:8px}.device-launchpad .controller{grid-area:2/2;gap:7px;aspect-ratio:1}.device-launchpad .controller .control{aspect-ratio:1;border-radius:3px;background:#111315;padding:3px;border:1px solid #131517;box-shadow:0 1px 2px #0008}.device-launchpad .control-hardware{position:absolute;inset:3px;width:auto;height:auto}.device-launchpad .pad-surface{height:100%;width:100%;border-radius:2px;background:linear-gradient(145deg,color-mix(in srgb,var(--control-color) 80%,#fff),var(--control-color));box-shadow:inset 0 0 8px #ffffff25;border:1px solid #ffffff15;opacity:.9}.device-launchpad .controller .control:not(.assigned) .pad-surface{background:linear-gradient(145deg,#d0d3db,#a4a9b4);opacity:.85}.device-launchpad .control-label{position:relative;z-index:1;white-space:normal;overflow-wrap:anywhere;color:#161c25;font-size:9px;font-weight:600;line-height:1.15;max-height:100%;overflow:hidden;padding:5px}.device-launchpad .controller .control[aria-pressed=true]{border-color:white;box-shadow:0 0 0 2px white;z-index:2}.pad-top{grid-area:1/2;display:grid;grid-template-columns:repeat(8,1fr);gap:7px}.pad-left{grid-area:2/1}.pad-right{grid-area:2/3}.pad-left,.pad-right{display:grid;grid-template-rows:repeat(8,1fr);gap:7px}.device-launchpad .device-decoration span{display:flex;align-items:center;justify-content:center;text-align:center;background:linear-gradient(140deg,#35373a,#2a2c2e);border:1px solid #151618;border-radius:2px;box-shadow:0 1px 2px #0005;font-size:7px;line-height:1.2;padding:2px;color:#b9bec4}.device-launchpad .pad-top .active{color:#7ce9ba;border-bottom:2px solid #7ce9ba}.pad-right span{flex-direction:column}.pad-right small{font-size:6px}.pad-bottom{grid-area:3/2;display:grid;gap:7px}.track-select,.track-functions{display:grid;grid-template-columns:repeat(8,1fr);gap:7px}.track-select{height:20px}.track-functions{height:32px}.device-launchpad .track-select span{color:#80cbb0;font-size:8px}.preview-note{font-size:11px;line-height:1.5;margin:0;color:#9facb3}@media(min-width:1400px){.device-xl3 .controller{grid-template-rows:repeat(3,96px) 205px repeat(2,40px)}.knob{width:39px;height:39px}.knob:before{left:17px}.knob:after{top:47px;left:13px}.control-label{font-size:10px}}
</style>
