import { inject, type InjectionKey } from 'vue'
import { createElixirMappingEngine } from '../elixir/mapping-context'
import { PARAMETER_REGISTRY, type Parameter } from '../elixir/parameter-registry'
import type { ChannelSettings } from '../elixir/channel-settings'
import type { ControllerTemplate } from '../core/mapping-engine/types'
export interface MidiMapperOptions {
  /** Full records merged by ID. New IDs are also accepted. */
  parameterOverrides?: readonly Parameter[]
  defaultChannels?: ChannelSettings
  /** Nonempty subset of the two built-in profile IDs. */
  availableDevices?: readonly string[]
  savedTemplates?: readonly ControllerTemplate[]
  /** Applied to the mapper root; use CSS custom properties or scoped host rules. */
  theme?: { className?: string; foreground?: string; background?: string; accent?: string }
  /** Where the host publishes the parameter-registry JSON (the same records, generated from the firmware): the
   * compact view's Config tab links it as "Download parameter JSON". Omitted: no link. */
  parameterJsonUrl?: string
}
export function createMapperContext(overrides: readonly Parameter[] = []) {
  const entries = new Map(PARAMETER_REGISTRY.map(p => [p.id, p]))
  for (const p of overrides) entries.set(p.id, structuredClone(p))
  const parameters = [...entries.values()]
  return { parameters, engine: createElixirMappingEngine(undefined, parameters) }
}
export const mapperContextKey: InjectionKey<ReturnType<typeof createMapperContext>> = Symbol('midi-mapper')
export function useMapperContext() { return inject(mapperContextKey, undefined) ?? createMapperContext() }
