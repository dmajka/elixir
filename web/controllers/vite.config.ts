import { execFileSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { defineConfig, type Plugin } from 'vitest/config'
import vue from '@vitejs/plugin-vue'
import { parseParameterDocument } from './src/elixir/registry-schema'

const ELIXIR = fileURLToPath(new URL('../..', import.meta.url))

/** virtual:elixir-parameters: Elixir's remote map, generated from the firmware (tools/remote_params.py) on every
 * build, dev server start and test run, checked (registry-schema.ts) and bundled: the page needs no fetch.
 * ELIXIR_PARAMS=path uses a JSON already generated (web/make_site.py: the same bytes it publishes). A JSON that is
 * missing or fails the checks stops the build: there is no fallback table. */
function elixirParameters(): Plugin {
  const id = 'virtual:elixir-parameters', resolved = '\0' + id
  let file = process.env.ELIXIR_PARAMS
  return {
    name: 'elixir-parameters',
    buildStart() {
      if (!file) {
        const out = `${ELIXIR}build/params`
        execFileSync(process.env.PYTHON || 'python3', [`${ELIXIR}tools/remote_params.py`, out], { stdio: ['ignore', 'ignore', 'inherit'] })
        file = `${out}/elixir-midi-parameters.json`
      }
      this.addWatchFile(file)
    },
    resolveId(source) { return source === id ? resolved : undefined },
    load(source) {
      if (source !== resolved) return
      const document = parseParameterDocument(JSON.parse(readFileSync(file!, 'utf8')))
      return `export default ${JSON.stringify(document)}`
    },
  }
}

export default defineConfig({
  plugins: [vue(), elixirParameters()],
  // Relative assets: the page works at any path (GitHub Pages' /elixir/controllers/, a local server's /controllers/).
  base: './',
  test: { environment: 'jsdom', include: ['src/tests/**/*.test.ts'] },
})
