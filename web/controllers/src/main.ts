import { createApp } from 'vue'
import MidiMapper from './components/MidiMapper.vue'
import AdminMidiMapper from './components/AdminMidiMapper.vue'
import type { MidiMapperOptions } from './integration/context'
import './site-layer.css'
import './style.css'

// Elixir's site (web/make_site.py): the page sits under the site's header, and the parameter JSON is published at
// the site's root (data/), next to this page's folder. ?view=admin: the full editor, monitor and hardware tools.
const header = document.querySelector<HTMLElement>('header.site')
if (header) {
  const fit = () => document.documentElement.style.setProperty('--host-nav-height', `${header.offsetHeight}px`)
  new ResizeObserver(fit).observe(header)
  fit()
}
const options: MidiMapperOptions = { parameterJsonUrl: '../data/elixir-midi-parameters.json' }
const admin = new URLSearchParams(location.search).get('view') === 'admin'
createApp(admin ? AdminMidiMapper : MidiMapper, { options }).mount('#app')
