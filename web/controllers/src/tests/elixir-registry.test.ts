import { describe, expect, it } from 'vitest'
import { channelWarnings, DEFAULT_CHANNEL_SETTINGS, validateChannelSettings, withTrackChannels } from '../elixir/channel-settings'
import { PARAMETER_REGISTRY, filterParameters, findParametersForMessage, getParameter, resolveParameter, TRACKS } from '../elixir/parameter-registry'
const defaults = { ...DEFAULT_CHANNEL_SETTINGS }
// Independent transcription of page 6, including every column of each row.
const trackChart: [string, number[]][] = [
  ['volume',[8,9,10,11,12,13,14,15]], ['pan',[16,17,18,19,20,21,22,23]],
  ['filter-frequency',[24,25,26,27,28,29,30,31]], ['resonance',[32,33,34,35,36,37,38,39]],
  ['decay',[40,41,42,43,44,45,46,47]], ['pitch',[48,49,50,51,52,53,54,55]],
  ['delay-send',[56,57,58,59,60,61,62,63]], ['fx-send',[64,65,66,67,68,69,70,71]],
  ['reverb-send',[72,73,74,75,76,77,78,79]], ['sample-rate',[80,81,82,83,84,85,86,87]],
  ['rush',[88,89,90,91,92,93,94,95]], ['density',[96,97,98,99,100,101,102,103]],
  ['mute',[104,105,106,107,108,109,110,111]], ['solo',[112,113,114,115,116,117,118,119]],
]
const globalKnobs: [number,string][] = [
  [8,'tempo'],[9,'shuffle'],[10,'morph'],[11,'sample-rate'],[12,'master-filter'],[13,'roll-rate'],[14,'repeat'],[15,'tape-time'],
  [16,'fx-wet'],[17,'decay'],[18,'tape-speed'],[21,'bus-dirt'],[22,'echo-to-reverb'],[23,'saturate'],
  [24,'reverb-level'],[25,'reverb-size'],[26,'reverb-damping'],[27,'master-dirt'],[28,'density'],[29,'ghost-velocity'],[30,'build'],[31,'vary'],
  [32,'macro-1'],[33,'macro-2'],[34,'macro-3'],[35,'macro-4'],[36,'drill'],[37,'jitter'],[38,'cuts'],[39,'pump'],
  [40,'comb-note'],[41,'comb-feedback'],[42,'crush'],[43,'reverse-length'],[44,'lfo-1-depth'],[45,'lfo-2-depth'],[46,'lfo-3-depth'],[47,'filter-resonance'],
  [96,'delay.time'],[97,'delay.feedback'],[98,'delay.tone'],[99,'delay.band'],[100,'delay.wow'],[101,'delay.glitch'],[102,'delay.route'],[103,'delay.reverb-to-fx'],
  [3,'ring.wet'],[4,'ring.slope'],[5,'ring.tone'],[7,'ring.tune'],[63,'ring.spread'],[2,'lfo-4-depth'],
]
const globalButtons: [number,string][] = [
  [48,'play-stop'],[49,'record'],[50,'fill'],[51,'repeat-held'],[52,'freeze'],[53,'throw-reverb'],[54,'throw-fx'],[55,'feedback-push'],
  [56,'tape-stop'],[57,'low-pass-sweep'],[58,'high-pass-sweep'],[59,'reverse'],[60,'pattern-previous'],[61,'pattern-next'],[62,'control-all'],
  [64,'select.d1'],[65,'select.d2'],[66,'select.d3'],[67,'select.sn'],[68,'select.cy'],[69,'select.ch'],[70,'select.oh'],[71,'control-all-revert'],
  [72,'roll.d1'],[73,'roll.d2'],[74,'roll.d3'],[75,'roll.sn'],[76,'roll.cy'],[77,'roll.ch'],[78,'roll.oh'],[79,'roll.selected'],
  [80,'drill.d1'],[81,'drill.d2'],[82,'drill.d3'],[83,'drill.sn'],[84,'drill.cy'],[85,'drill.ch'],[86,'drill.oh'],[87,'drill.selected'],
  [88,'effect.half'],[89,'effect.jitter'],[90,'effect.cuts'],[91,'effect.short'],[92,'effect.pump'],[93,'effect.comb'],[94,'effect.space'],[95,'effect.crush'],
  [104,'density-exclude.d1'],[105,'density-exclude.d2'],[106,'density-exclude.d3'],[107,'density-exclude.sn'],[108,'density-exclude.cy'],[109,'density-exclude.ch'],[110,'density-exclude.oh'],[111,'density-all-in'],
]
describe('complete page 6 remote map', () => {
  it.each(trackChart)('resolves every track column for %s', (key,numbers) => {
    numbers.forEach((number,i) => {
      const id=`tracks.${TRACKS[i]}.${key}`, p=getParameter(id)
      expect(p.number).toBe(number)
      expect(p.scope).toBe('remote-tracks')
      expect(Array.from(resolveParameter(id,defaults,{value:73}).bytes)).toEqual([0xbe,number,73])
      expect(Array.from(resolveParameter(id,{remoteTracks:3,remoteGlobal:12},{value:73}).bytes)).toEqual([0xb2,number,73])
      if (key==='mute'||key==='solo') {
        expect(p.buttonBehavior).toBe('press-or-state')
        expect(Array.from(resolveParameter(id,defaults,{messageType:'note'}).bytes)).toEqual([0x9e,number,127])
        expect(Array.from(resolveParameter(id,defaults,{messageType:'note',release:true}).bytes)).toEqual([0x8e,number,0])
      } else expect(p.alternateMessageTypes).toEqual([])
    })
  })
  it.each(globalKnobs)('resolves global knob CC %i (%s)', (number,key) => {
    const id=`global.${key}`
    expect(getParameter(id).controlType).toBe('continuous')
    expect(Array.from(resolveParameter(id,defaults,{value:64}).bytes)).toEqual([0xbf,number,64])
    expect(Array.from(resolveParameter(id,{remoteTracks:1,remoteGlobal:2},{value:64}).bytes)).toEqual([0xb1,number,64])
  })
  it.each(globalButtons)('resolves CC/note press and release %i (%s)', (number,key) => {
    const id=`global.${key}`
    expect(Array.from(resolveParameter(id,defaults).bytes)).toEqual([0xbf,number,127])
    expect(Array.from(resolveParameter(id,defaults,{release:true}).bytes)).toEqual([0xbf,number,0])
    expect(Array.from(resolveParameter(id,defaults,{messageType:'note'}).bytes)).toEqual([0x9f,number,127])
    expect(Array.from(resolveParameter(id,defaults,{messageType:'note',release:true}).bytes)).toEqual([0x8f,number,0])
  })
  it('treats roll faders as value-dependent CCs, not note buttons', () => {
    ;[112,113,114,115,116,117,118,119].forEach((number,i) => {
      const id=`global.roll-fader.${TRACKS[i]}`
      for (const value of [0,1,32,33,127]) expect(Array.from(resolveParameter(id,defaults,{value}).bytes)).toEqual([0xbf,number,value])
      expect(getParameter(id).interpretation).toContain('33–127')
      expect(() => resolveParameter(id,defaults,{messageType:'note'})).toThrow('Unsupported')
    })
  })
  it('does not invent a button at 63 from the tap/hold annotation', () => {
    expect(findParametersForMessage('cc',63,16,defaults).map(p=>p.id)).toEqual(['global.ring.spread'])
    expect(findParametersForMessage('note',63,16,defaults)).toEqual([])
  })
})
describe('page 5 shared messages and real-time', () => {
  it('resolves all 32 patterns on both remote groups as two-byte program changes', () => {
    for(const [prefix,channel] of [['tracks',15],['global',16]] as const) for(let pattern=1;pattern<=32;pattern++) {
      const r=resolveParameter(`${prefix}.pattern.${pattern}`,defaults)
      expect(Array.from(r.bytes)).toEqual([0xc0|(channel-1),pattern-1])
      expect(r.messageType).toBe('program-change')
    }
  })
  it('includes shared kit/morph/master controls with their documented bounds', () => {
    for(const [prefix,channel] of [['tracks',15],['global',16]] as const) {
      for(const [key,number,max] of [['master-volume',124,127],['morph-kit',125,63],['slot-kit',126,63],['factory-kit',127,84]] as const) {
        const id=`${prefix}.${key}`
        expect(Array.from(resolveParameter(id,defaults,{value:max}).bytes)).toEqual([0xb0|(channel-1),number,max])
        expect(()=>resolveParameter(id,defaults,{value:max+1})).toThrow('Value')
      }
    }
  })
  it.each([['clock',248],['start',250],['stop',252]] as const)('keeps %s channel-less', (key,status) => {
    const r=resolveParameter(`system.${key}`,defaults)
    expect(r.channel).toBeUndefined(); expect(Array.from(r.bytes)).toEqual([status])
    expect(()=>resolveParameter(`system.${key}`,defaults,{value:0})).toThrow('Value')
  })
})
describe('registry contracts', () => {
  it('contains 310 uniquely identified parameters with complete metadata', () => {
    expect(PARAMETER_REGISTRY).toHaveLength(310)
    expect(new Set(PARAMETER_REGISTRY.map(p=>p.id)).size).toBe(310)
    for(const p of PARAMETER_REGISTRY) {
      expect(p.name && p.shortName && p.category && p.interpretation).toBeTruthy()
      expect(p.documentation.source).toMatch(/^firmware\/src\/(remote|midi)\.c$/)
      expect(p.documentation.symbol).toBeTruthy()
      expect(Object.isFrozen(p)).toBe(true)
      const {bytes}=resolveParameter(p.id,defaults)
      expect(bytes.length).toBe(p.messageType==='realtime'?1:p.messageType==='program-change'?2:3)
      for(const type of [p.messageType,...p.alternateMessageTypes]) expect(findParametersForMessage(type,p.number,p.channelGroup?(withTrackChannels(defaults) as Record<string,number>)[p.channelGroup]:undefined,defaults)).toContain(p)
    }
  })
  it('plays each track on its own channel: note 60 at the pad velocity, seven hits', () => {
    const hits = PARAMETER_REGISTRY.filter(p => p.scope === 'track-voices')
    expect(hits.map(p => p.id)).toEqual(['d1','d2','d3','sn','cy','ch','oh'].map(t => `tracks.${t}.hit`))
    hits.forEach((p, i) => {
      expect(p).toMatchObject({ messageType: 'note', number: 60, controlType: 'button', channelGroup: `track${p.track!.toUpperCase()}` })
      expect([...resolveParameter(p.id, defaults, { value: 37 }).bytes]).toEqual([0x90 | i, 60, 37])
    })
    expect(findParametersForMessage('note', 60, 4, defaults).map(p => p.id)).toEqual(['tracks.sn.hit'])
  })
  it('preserves selected, CH/OH, conventional vs remote, and contextual actions', () => {
    expect(getParameter('tracks.selected.volume').number).toBe(15)
    expect(getParameter('tracks.ch.decay').number).toBe(45)
    expect(getParameter('tracks.oh.decay').number).toBe(46)
    expect(getParameter('tracks.d1.volume').number).toBe(8) // conventional D1 volume is CC89, not remote
    expect(getParameter('global.select.d1').interpretation).toContain('Control All')
    expect(getParameter('global.control-all').buttonBehavior).toBe('tap-latch-hold')
    expect(getParameter('tracks.d1.density').interpretation).toContain('64')
    expect(getParameter('global.reverb-level').feedback?.controllerCompatibility).toBe('unverified')
  })
  it('filters text, category, track, scope, and alternate note type', () => {
    expect(filterParameters({query:' ReVeRb ',category:'Sends',track:'ch'}).map(p=>p.id)).toEqual(['tracks.ch.reverb-send'])
    expect(filterParameters({scope:'remote-tracks',messageType:'note'})).toHaveLength(16)
    expect(filterParameters({scope:'remote-global',messageType:'note'})).toHaveLength(55)
  })
  it('returns namespace collisions rather than choosing an arbitrary match', () => {
    const matches=findParametersForMessage('cc',8,5,{remoteTracks:5,remoteGlobal:5})
    expect(matches.map(p=>p.id)).toEqual(['tracks.d1.volume','global.tempo'])
  })
  it('rejects unknown IDs, non-button releases, invalid values, and message substitutions', () => {
    expect(()=>getParameter('global.fake')).toThrow('Unknown')
    expect(()=>resolveParameter('global.tempo',defaults,{release:true})).toThrow('buttons')
    expect(()=>resolveParameter('global.tempo',defaults,{messageType:'note'})).toThrow('Unsupported')
    for(const value of [-1,128,NaN,1.5]) expect(()=>resolveParameter('global.tempo',defaults,{value})).toThrow('Value')
    expect(()=>resolveParameter('global.play-stop',defaults,{release:true,value:5})).toThrow('fixed')
  })
})
describe('channel settings', () => {
  it('defaults to 15/16 and warns without mutating settings', () => {
    expect(defaults).toEqual({remoteTracks:15,remoteGlobal:16}); expect(channelWarnings(defaults)).toEqual([])
    const same={remoteTracks:10,remoteGlobal:10}, before={...same}
    expect(channelWarnings(same).map(w=>w.code)).toEqual(['remote-overlap','global-overlap','global-overlap'])
    expect(same).toEqual(before)
    expect(channelWarnings({remoteTracks:8,remoteGlobal:9},{tracks:[8,9],global:11}).map(w=>w.code)).toEqual(['track-overlap','track-overlap'])
  })
  it('accepts all channel choices and rejects out-of-range or fractional channels', () => {
    for(let channel=1;channel<=16;channel++) {
      expect(Array.from(resolveParameter('tracks.d1.volume',{remoteTracks:channel,remoteGlobal:16},{value:0}).bytes)).toEqual([0xb0|(channel-1),8,0])
    }
    for(const channel of [0,17,NaN,1.5]) {
      expect(()=>validateChannelSettings({remoteTracks:channel,remoteGlobal:16})).toThrow('channel')
      expect(()=>resolveParameter('global.tempo',{remoteTracks:15,remoteGlobal:channel})).toThrow('channel')
    }
  })
})
