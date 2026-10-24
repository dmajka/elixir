// SPDX-License-Identifier: GPL-3.0-only
// Kettle's library (0.7): the protocol of firmware/src/library.c, LXR-02 kit files and backup files, for the library
// page (Web MIDI to an FM-1) and the emulator (its WebAssembly). No DOM here.
//
// A transport is { request(wire: Uint8Array) -> Promise<Uint8Array | null> }: one SysEx message out, the answer
// (F0 .. F7) back, or null after its timeout.

export const STATUS = ["ok", "bad message", "bad arguments", "busy", "flash error", "checksum mismatch", "too big",
  "empty slot", "out of order"];
export const SLOT_NAMES = ["Project 1", "Project 2", "Project 3", "Project 4", "Session (AUTO)"];
const LXR_PARAMS = 247;                              // an LXR-02 kit: 8 name bytes, then these

function pack7(bytes) {
  const out = [];
  let acc = 0, nb = 0;
  for (const b of bytes) {
    acc |= b << nb;
    nb += 8;
    while (nb >= 7) { out.push(acc & 0x7f); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7f);
  return out;
}
function unpack7(seven) {
  const out = [];
  let acc = 0, nb = 0;
  for (const s of seven) {
    acc |= (s & 0x7f) << nb;
    nb += 7;
    if (nb >= 8) { out.push(acc & 0xff); acc >>>= 8; nb -= 8; }
  }
  return Uint8Array.from(out);
}
const u32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
const rd32 = (a, i) => (a[i] | (a[i + 1] << 8) | (a[i + 2] << 16) | (a[i + 3] << 24)) >>> 0;
const nameBytes = (s) => { const b = new Array(8).fill(32); [...(s || "")].slice(0, 8).forEach((c, i) => { const k = c.charCodeAt(0); b[i] = k >= 32 && k < 127 ? k : 32; }); return b; };
export const nameOf = (bytes) => String.fromCharCode(...[...bytes].map((c) => (c >= 32 && c < 127 ? c : 32))).trimEnd();

export function message(cmd, tag, payload = []) {
  const body = [cmd, tag, ...payload];
  const sum = body.reduce((a, b) => a + b, 0);
  body.push(~sum & 255);
  return Uint8Array.from([0xf0, 0x7d, 0x4b, 0x54, ...pack7(body), 0xf7]);
}
export function isLibrary(wire) {
  return wire.length > 5 && wire[0] === 0xf0 && wire[1] === 0x7d && wire[2] === 0x4b && wire[3] === 0x54;
}
function parse(wire) {                               // -> { cmd, tag, status, data } or null
  if (!isLibrary(wire) || wire[wire.length - 1] !== 0xf7) return null;
  const d = unpack7(wire.subarray(4, wire.length - 1));
  if (d.length < 4) return null;
  let sum = 0;
  for (let i = 0; i < d.length - 1; i++) sum += d[i];
  if ((~sum & 255) !== d[d.length - 1]) return null;
  return { cmd: d[0] & 0x7f, tag: d[1], status: d[2], data: d.subarray(3, d.length - 1) };
}

export class Library {
  constructor(transport) { this.t = transport; this.tag = 0; }

  async call(cmd, payload = [], tries = 1) {
    for (let k = 0; k < tries; k++) {
      const tag = (this.tag = (this.tag + 1) & 0x7f);
      const ans = await this.t.request(message(cmd, tag, payload));
      const r = ans && parse(ans);
      if (!r || r.cmd !== cmd || r.tag !== tag) continue;
      if (r.status) {
        const e = new Error(STATUS[r.status] || "error " + r.status);
        e.status = r.status;
        throw e;
      }
      return r.data;
    }
    throw new Error("no answer from Kettle");
  }

  async info() {
    const d = await this.call(0x01, [], 3);
    const info = { proto: d[0], version: nameOf(d.subarray(1, 9)).replace(/\0/g, ""), nlog: d[9], nkits: d[10],
                   kitLen: d[11] | (d[12] << 8), loaded: d[13], kitLoaded: d[14], slots: [] };
    for (let l = 0, i = 15; l < info.nlog; l++, i += 18)
      info.slots.push({ exists: !!d[i], format: d[i + 1], len: rd32(d, i + 2), seq: rd32(d, i + 6),
                        name: nameOf(d.subarray(i + 10, i + 18)) });
    return info;
  }

  async kitNames(n = 64) {
    const names = [];
    for (let first = 0; first < n; first += 32) {
      const d = await this.call(0x02, [first, 32], 3);
      for (let k = 0; k < d[1]; k++) names.push(nameOf(d.subarray(2 + k * 8, 10 + k * 8)));
    }
    return names;
  }

  // a project's image (its 256-byte header block and payload), as it is in flash
  async readProject(slot, onProgress) {
    const head = await this.call(0x10, [slot, ...u32(0), 0, 1], 3);
    const len = 256 + rd32(head, 7 + 16);          // (the header: magic, version, logical, seq, len ..)
    const img = new Uint8Array(len);
    for (let off = 0; off < len; off += 256) {
      const n = Math.min(256, len - off);
      const d = await this.call(0x10, [slot, ...u32(off), n & 255, n >> 8], 3);
      img.set(d.subarray(7, 7 + n), off);
      if (onProgress) onProgress(off + n, len);
    }
    if (!checkImage(img).ok) throw new Error("the project read back damaged; try again");
    return img;
  }

  async writeProject(slot, img, name = "", onProgress) {
    const c = checkImage(img);
    if (!c.ok) throw new Error(c.why);
    await this.call(0x20, [slot, ...u32(img.length), ...(name ? nameBytes(name) : new Array(8).fill(0))]);
    try {
      for (let off = 0; off < img.length; off += 256) {
        const n = Math.min(256, img.length - off);
        await this.call(0x21, [...u32(off), n & 255, n >> 8, ...img.subarray(off, off + n)]);
        if (onProgress) onProgress(off + n, img.length);
      }
      await this.call(0x22);
    } catch (e) {
      try { await this.call(0x23); } catch (_) { /* (it gives up by itself after 10 s) */ }
      throw e;
    }
  }

  loadProject(slot) { return this.call(0x24, [slot]); }
  saveProject(slot, name) { return this.call(0x25, [slot, ...nameBytes(name)]); }

  // data: the kit layout (an LXR .SND's 247 bytes, or a whole Kettle kit)
  kitPut(idx, name, data, loadNow = false) {
    return this.call(0x30, [idx, loadNow ? 1 : 0, ...nameBytes(name), data.length & 255, data.length >> 8, ...data]);
  }
  async kitGet(idx) {
    const d = await this.call(0x31, [idx], 3);
    const n = d[9] | (d[10] << 8);
    return { name: nameOf(d.subarray(1, 9)), data: d.slice(11, 11 + n) };
  }
}

// ----------------------------------------------------------------------------------------------- files ---

const MAGIC = 0x4a52504b;                            // "KPRJ" (project.c PROJ_MAGIC)

function crc32(bytes) {                              // zlib's, as storage.c's
  let c = 0xffffffff;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c & 1 ? (c >>> 1) ^ 0xedb88320 : c >>> 1;
  }
  return ~c >>> 0;
}

// a project image: the header's magic, its own CRC, the payload's length and CRC
export function checkImage(img) {
  if (!(img instanceof Uint8Array) || img.length < 257) return { ok: false, why: "not a Kettle project" };
  const h = img.subarray(0, 48);                    // (proj_hdr_t: 48 bytes, its own CRC last)
  if (rd32(h, 0) !== MAGIC) return { ok: false, why: "not a Kettle project" };
  if (rd32(h, 44) !== crc32(h.subarray(0, 44))) return { ok: false, why: "the project's header is damaged" };
  const len = rd32(h, 16);
  if (img.length !== 256 + len) return { ok: false, why: "the project file is cut short" };
  if (rd32(h, 20) !== crc32(img.subarray(256))) return { ok: false, why: "the project's data is damaged" };
  return { ok: true, format: rd32(h, 4), name: nameOf(h.subarray(24, 32)) };
}

// a backup file: "KTBK", a count, then (slot, length, image) for each project
export function packBackup(entries) {
  const total = 8 + entries.reduce((a, e) => a + 8 + e.img.length, 0);
  const out = new Uint8Array(total);
  out.set([0x4b, 0x54, 0x42, 0x4b, ...u32(entries.length)]);
  let i = 8;
  for (const e of entries) {
    out.set([...u32(e.slot), ...u32(e.img.length)], i);
    out.set(e.img, i + 8);
    i += 8 + e.img.length;
  }
  return out;
}
export function unpackBackup(buf) {                  // -> [{ slot, img }] (also takes a single project image)
  const b = new Uint8Array(buf);
  if (rd32(b, 0) === MAGIC) return [{ slot: -1, img: b }];
  if (!(b[0] === 0x4b && b[1] === 0x54 && b[2] === 0x42 && b[3] === 0x4b)) throw new Error("not a Kettle backup");
  const n = rd32(b, 4), out = [];
  let i = 8;
  for (let k = 0; k < n; k++) {
    const slot = rd32(b, i), len = rd32(b, i + 4);
    if (i + 8 + len > b.length) throw new Error("the backup is cut short");
    out.push({ slot, img: b.slice(i + 8, i + 8 + len) });
    i += 8 + len;
  }
  return out;
}

// an LXR-02 kit file (.SND): its name and parameters. Its number and name come from the file name (NN-NAME.SND).
export function parseSnd(buf, fileName = "") {
  const b = new Uint8Array(buf);
  if (b.length < 8 + 16 || b.length > 8 + LXR_PARAMS + 64) throw new Error("not an LXR kit file");
  const m = /^(\d+)-(.*)\.snd$/i.exec(fileName.split("/").pop() || "");
  let name = nameOf(b.subarray(0, 8)).replace(/\0/g, "").trim();
  if (!name && m) name = m[2].replace(/_+$/, "");
  return { name: name || "Kit", data: b.slice(8, 8 + Math.min(LXR_PARAMS, b.length - 8)), number: m ? +m[1] : null,
           file: fileName };
}
// the kits of an LXR project folder (or loose .SND files), in their numbers' order; "Init"/empty kits left out
export async function sndFiles(files, { skipInit = true } = {}) {
  const kits = [];
  for (const f of files) {
    if (!/\.snd$/i.test(f.name)) continue;
    try {
      const k = parseSnd(await f.arrayBuffer(), f.webkitRelativePath || f.name);
      if (skipInit && (/^(init|initk|initkit|empty|in\s*k)$/i.test(k.name.replace(/\s+/g, " ").trim()) ||
                       /-(INITK|EMPTY|IN__K)\.SND$/i.test(f.name))) continue;
      kits.push(k);
    } catch (_) { /* (not a kit) */ }
  }
  kits.sort((a, b) => (a.file.split("/").slice(0, -1).join("/").localeCompare(b.file.split("/").slice(0, -1).join("/")))
                      || (a.number ?? 999) - (b.number ?? 999) || a.file.localeCompare(b.file));
  return kits;
}
// a kit as an LXR-02 .SND file (its first 247 bytes: Kettle's own settings aren't in the LXR's format)
export function sndBytes(name, data) {
  const out = new Uint8Array(8 + LXR_PARAMS);
  out.set(nameBytes(name).map((c) => c), 0);
  out.set(data.subarray(0, LXR_PARAMS), 8);
  return out;
}

// ----------------------------------------------------------------------------------------- transports ---

// Web MIDI: the FM-1's port (Kettle names itself "Kettle"), SysEx allowed
export async function midiTransport(access, log = () => {}) {
  const outs = [...access.outputs.values()];
  for (const input of access.inputs.values()) {
    if (input.state === "disconnected" || !/kettle|fm-1|felucca|usb-midi/i.test(input.name || "")) continue;
    const output = outs.find((o) => o.name === input.name && o.state !== "disconnected");
    if (!output) continue;
    await input.open();
    await output.open();
    let waiter = null, timeout = 1500;               // (short while probing a port; then room for a flash erase)
    input.onmidimessage = (ev) => { if (waiter && isLibrary(ev.data)) { const w = waiter; waiter = null; w(ev.data); } };
    const t = {
      name: input.name,
      request(wire, ms = timeout) {
        return new Promise((resolve) => {
          const tm = setTimeout(() => { waiter = null; resolve(null); }, ms);
          waiter = (d) => { clearTimeout(tm); resolve(new Uint8Array(d)); };
          output.send(wire);
        });
      },
      close() { input.onmidimessage = null; },
    };
    try {
      await new Library(t).call(0x01, [], 2);
      timeout = 6000;
      log("found " + input.name);
      return t;
    } catch (_) {
      t.close();
    }
  }
  return null;
}

// the emulator: its WebAssembly exports (web_lib_buf, web_lib_in, web_lib_out) and memory
export function wasmTransport(exp, memory) {
  return {
    name: "emulator",
    async request(wire) {
      const mem = () => new Uint8Array(memory.buffer);
      mem().set(wire, exp.web_lib_buf());
      const n = exp.web_lib_in(wire.length);
      return n ? mem().slice(exp.web_lib_out(), exp.web_lib_out() + n) : null;
    },
  };
}
