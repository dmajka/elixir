<script setup lang="ts">
// SPDX-License-Identifier: GPL-3.0-only
/** A sequencer (src/devices/sequencers.ts): its instrument definitions, from the site's data/<folder>/, with how to set
 * them up. Nothing to edit here: download, load, play. */
import { ref, watch } from 'vue'
import { REGISTRY_SOURCE } from '../elixir/parameter-registry'
import type { Sequencer } from '../devices/sequencers'

const props = defineProps<{ sequencer: Sequencer; dataUrl: string; tracksChannel: number; globalChannel: number }>()
const base = () => `${props.dataUrl}${props.sequencer.folder}/`
const texts = ref<Record<string, string>>({})
watch(() => props.sequencer, s => {
  for (const f of s.files)                            // (the published bytes, as a preview; the links work without it)
    if (!(f.name in texts.value))
      fetch(base() + f.name).then(r => r.ok ? r.text() : '').then(t => { if (t) texts.value[f.name] = t }).catch(() => {})
}, { immediate: true })
</script>
<template>
  <div class="hx">
    <section class="hx-main">
      <h2 class="hx-title">{{ sequencer.name }}</h2>
      <p class="hx-lead">{{ sequencer.lead }} From Elixir {{ REGISTRY_SOURCE.firmwareVersion }}.</p>
      <div class="hx-cards">
        <article v-for="f in sequencer.files" :key="f.name" class="hx-card">
          <h3>{{ f.title }} <code>{{ f.name }}</code></h3>
          <p>{{ f.what }}</p>
          <a class="hx-get" :href="base() + f.name" :download="f.name">Download {{ f.name }} ↓</a>
          <details v-if="texts[f.name]" class="hx-file"><summary>The file</summary><pre>{{ texts[f.name] }}</pre></details>
        </article>
      </div>
      <p v-if="sequencer.legend" class="hx-legend">{{ sequencer.legend }}</p>
    </section>
    <aside class="hx-side">
      <h2 class="hx-title">Setting up</h2>
      <ol class="hx-steps">
        <li v-for="[what, how] in sequencer.steps(tracksChannel, globalChannel)" :key="what"><b>{{ what }}:</b> {{ how }}</li>
        <li v-if="tracksChannel !== 15 || globalChannel !== 16"><b>Other channels:</b> {{ sequencer.otherChannels }}</li>
      </ol>
      <p class="hx-hint">Not yet tried on the {{ sequencer.name }}. The format: <a :href="sequencer.source.href">{{ sequencer.source.text }}</a>; the numbers: <a href="../manual/#the-remote-map">the remote map</a>.</p>
    </aside>
  </div>
</template>
<style scoped>
.hx{display:grid;grid-template-columns:minmax(0,1fr) 330px;flex:1;min-height:0}
.hx-main{overflow:auto;padding:22px 24px;background:var(--well,#201e1b)}
.hx-side{overflow:auto;padding:22px 18px;border-left:1px solid var(--line,#2d2a25);background:var(--bg,#131210)}
.hx-title{font-size:18px;font-weight:650;margin:0 0 6px}
.hx-lead{max-width:66ch;color:var(--dim,#a29b8e);font-size:14px;line-height:1.55;margin:0 0 18px}
.hx-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px;max-width:980px}
.hx-card{background:var(--card,#1b1a17);border:1px solid var(--line,#2d2a25);border-radius:10px;padding:16px;min-width:0}
.hx-card h3{font-size:15px;margin:0 0 6px;display:flex;flex-wrap:wrap;align-items:baseline;gap:8px}
.hx-card h3 code{font-size:12.5px;font-weight:400;color:var(--dim,#a29b8e);overflow-wrap:anywhere}
.hx-card p{font-size:14px;line-height:1.55;margin:0 0 12px}
.hx-get{display:inline-block;font-size:14px;font-weight:600;overflow-wrap:anywhere}
.hx-file{margin-top:12px}
.hx-file summary{cursor:pointer;font-size:13px;color:var(--dim,#a29b8e)}
.hx-file pre{max-height:340px;overflow:auto;margin:8px 0 0;padding:10px;border-radius:7px;background:var(--well,#201e1b);border:1px solid var(--line,#2d2a25);font-size:12px;line-height:1.45;white-space:pre}
.hx-legend{max-width:980px;font-size:13px;line-height:1.55;color:var(--dim,#a29b8e);margin:16px 0 0}
.hx-steps{margin:8px 0 6px;padding-left:20px;font-size:14px;line-height:1.55}
.hx-steps li{margin-bottom:10px}
.hx-hint{font-size:13px;line-height:1.5;color:var(--dim,#a29b8e)}
@media (max-width:900px){.hx{grid-template-columns:minmax(0,1fr) 280px}}
@media (max-width:640px){.hx{grid-template-columns:1fr}.hx-side{border-left:0;border-top:1px solid var(--line,#2d2a25)}.hx-main{padding:16px}.hx-side{padding:16px}}
</style>
