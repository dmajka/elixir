// SPDX-License-Identifier: GPL-3.0-only
// Elixir 0.14: the install page's updater (web/fm1ota.js, Felucca's) and its device check (web/install.js) against
// simulated FM-1s over a mock Web MIDI, with the real package (build/elixir.fwsc). Above all the upgrade from Kettle
// (Elixir's name up to 0.13): a unit whose port is called "Kettle" must be found, updated, and found again afterwards
// under either name.
//
//   node tests/install_test.mjs [PACKAGE.fwsc]
//
// Time runs 100x faster (setTimeout and Date.now scaled), so the updater's waits take milliseconds.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const SPEED = 100;
const realSet = globalThis.setTimeout, realNow = Date.now, t0 = realNow();
globalThis.setTimeout = (f, ms = 0, ...a) => realSet(f, ms / SPEED, ...a);
Date.now = () => t0 + (realNow() - t0) * SPEED;

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const { Updater, pack7, unpack7 } = await import(join(ROOT, "web/fm1ota.js"));
const { productOf, logicalImage } = await import(join(ROOT, "web/fm1pkg.js"));
const { describe, firmwareOf } = await import(join(ROOT, "web/install.js"));

const pkg = new Uint8Array(readFileSync(process.argv[2] || join(ROOT, "build/elixir.fwsc")));
const PRODUCT = productOf(pkg), IMAGE = logicalImage(pkg);

let failed = 0;
const check = (ok, what) => { console.log(`  ${ok ? "ok  " : "FAIL"} ${what}`); if (!ok) failed++; };
const later = (f, ms = 5) => setTimeout(f, ms);
const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// ------------------------------------------------------------------ mock Web MIDI
class Input {
  constructor(name) { this.name = name; this.state = "connected"; this.onmidimessage = null; }
  async open() { if (this.state !== "connected") throw new Error("disconnected"); }
  deliver(bytes) { if (this.state === "connected" && this.onmidimessage) this.onmidimessage({ data: Uint8Array.from(bytes) }); }
}
class Output {
  constructor(name, sink) { this.name = name; this.state = "connected"; this.sink = sink; this.sent = 0; }
  async open() { if (this.state !== "connected") throw new Error("disconnected"); }
  send(bytes) {
    if (this.state !== "connected") throw new Error("InvalidStateError: the port is disconnected");
    this.sent++;
    this.sink(Array.from(bytes));
  }
}
class Access {
  constructor() { this.inputs = new Map(); this.outputs = new Map(); this.n = 0; }
  add(name, sink) {
    const k = `p${this.n++}`, i = new Input(name), o = new Output(name, sink);
    this.inputs.set(k, i); this.outputs.set(k, o);
    return { k, i, o };
  }
  remove(p) { p.i.state = p.o.state = "disconnected"; this.inputs.delete(p.k); this.outputs.delete(p.k); }
}

// ------------------------------------------------------------------ a simulated FM-1
const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

function identity(text) {
  const d = new Array(34).fill(0);
  d[1] = 0x59; d[2] = 0x11;
  [...text].forEach((c, i) => { d[6 + i] = c.charCodeAt(0); });
  return [0xF0, ...pack7(d), 0xF7];
}
function request(addr, len, fl = 0) {
  const u = [0, 0x59, 0x30, 0, 0, 0, fl, addr & 255, (addr >>> 8) & 255, (addr >>> 16) & 255, (addr >>> 24) & 255,
    len & 255, (len >> 8) & 255, 0];
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  u.push(~s & 255);
  return [0xF0, ...pack7(u), 0xF7];
}
function answer(pkt) {
  const b = unpack7(pkt.slice(1, -1));
  if (b.length < 14 || b[1] !== 0x59 || b[2] !== 0x30) return null;
  const addr = (b[7] | (b[8] << 8) | (b[9] << 16) | (b[10] << 24)) >>> 0, len = b[11] | (b[12] << 8);
  return { addr, data: b.slice(14, 14 + len) };
}

// app: { name (the port), id (FM-1_xxx) }; after: the port's name once the new firmware runs (a computer may keep
// the old one); cut: in the loader's write, the cable is pulled after this many requests
class FM1 {
  constructor(access, app, { after = "Elixir", cut = 0 } = {}) {
    this.access = access; this.after = after; this.cut = cut; this.bad = 0; this.writes = 0;
    this.plug(app.name, app.id, "app");
  }
  plug(name, id, mode) {
    this.mode = mode; this.id = id; this.queue = []; this.served = 0;
    this.port = this.access.add(name, (b) => this.receive(b));
  }
  unplug() { this.access.remove(this.port); }
  reboot(name, id, mode, ms = 300) { this.unplug(); later(() => this.plug(name, id, mode), ms); }
  send(b) { const p = this.port; later(() => p.i.deliver(b)); }
  receive(b) {
    if (eq(b, HS_QUERY)) return this.send(identity(this.id));
    if (eq(b, UPGRADE)) return this.upgrade();
    const r = answer(b);
    if (!r || !this.queue.length) return;
    const want = this.queue.shift();
    if (r.addr !== want.addr) { this.bad++; return; }
    if (want.addr < 0xE0000000 && !eq(r.data, Array.from(IMAGE.subarray(want.addr, want.addr + want.len)))) this.bad++;
    this.served++;
    if (this.queue.length) {
      if (this.mode === "ota" && this.cut && this.served === this.cut) { this.cut = 0; this.unplug(); return; }
      return this.ask();
    }
    if (this.mode === "app") this.reboot("Felucca Update", "ota-FM-1_900", "ota");     // stage 1 done: the loader
    else { this.writes++; this.reboot(this.after, PRODUCT, "app"); }                    // written: the new firmware
  }
  ask() { const q = this.queue[0]; this.send(request(q.addr, q.len)); }
  upgrade() {
    const reads = [];
    if (this.mode === "app") {                       // stage 1: parts of the package, then "verified"
      for (let a = 0; a < 20 * 512; a += 512) reads.push({ addr: a, len: 512 });
      reads.push({ addr: 0xE0000000, len: 8 });
    } else {                                         // the loader: the image (every 40th part here), then "written"
      for (let a = 0; a < IMAGE.length; a += 512 * 40) reads.push({ addr: a, len: Math.min(512, IMAGE.length - a) });
      reads.push({ addr: IMAGE.length - 256, len: 256 }, { addr: 0xF0000000, len: 8 });
    }
    this.queue = reads; this.served = 0;
    later(() => this.ask(), 50);
  }
}

// the install page's run(): resume when the loader is waiting, else a full install
async function run(access) {
  const up = new Updater(access, () => {});
  const ota = await up.find((id) => /^ota-/i.test(id.model));
  if (ota) { ota.link.close(); return (await up.resume(IMAGE, () => {}, PRODUCT)) ? PRODUCT : null; }
  return up.install(IMAGE, PRODUCT, () => {});
}
async function check_(access) {
  const dev = await new Updater(access, () => {}).find(null);
  if (dev) dev.link.close();
  return dev;
}

async function scenario(title, app, opts, expect) {
  console.log(title);
  const access = new Access(), unit = new FM1(access, app, opts);
  const other = access.add("Elektron Digitakt", () => {});           // other gear: never written to
  const before = await check_(access);
  check(before && expect.before.test(describe(before)), `check: "${before ? describe(before) : "not found"}"`);
  let result = null, err = null;
  try { result = await run(access); } catch (e) { err = e; }
  if (expect.cutOff) {
    check(err && err.code === "lost", `the cable pulled during the write: ${err ? err.code : "no error"}`);
    later(() => unit.plug("Felucca Update", "ota-FM-1_900", "ota"), 100);   // plugged in again: still the loader
    await new Promise((r) => realSet(r, 20));
    const mid = await check_(access);
    check(mid && /update mode/.test(describe(mid)), "check: in update mode");
    try { result = await run(access); err = null; } catch (e) { err = e; }
  }
  check(!err && result === PRODUCT, `installed: ${err ? err.code + " " + err.message : result}`);
  check(unit.bad === 0, "every part served was the package's");
  check(unit.writes === 1, "written once");
  check(other.o.sent === 0, "nothing sent to the Digitakt");
  const after = await check_(access);
  check(after && expect.after.test(describe(after)), `check after: "${after ? describe(after) : "not found"}"`);
}

console.log(`package ${PRODUCT}, image ${IMAGE.length} B`);
check(/^FM-1_9\d\d$/.test(PRODUCT), "an alternative firmware's identity (FM-1_9xx), as Kettle's");
check(IMAGE.length < 0x97000, "the image ends below the projects (0x97000)");
check(firmwareOf("Kettle") === "elixir" && firmwareOf("2- Kettle") === "elixir" && firmwareOf("Elixir") === "elixir" &&
      firmwareOf("Felucca Update") === "felucca" && firmwareOf("Digitakt") === null, "port names");
// what Kettle's own page (0.13) matched: it wouldn't find the unit once it is called Elixir; this page finds both
const OLD = /fm-1|felucca|kettle|ota|composite|sinco|usb-midi/i;
check(!OLD.test("Elixir"), "(Kettle 0.13's page doesn't see a unit named Elixir: use this page)");

await scenario("Kettle 0.13 -> Elixir", { name: "Kettle", id: "FM-1_900" }, {},
  { before: /Elixir is installed/, after: /Elixir is installed/ });
await scenario("Kettle -> Elixir, the computer keeps the name Kettle", { name: "Kettle", id: "FM-1_900" },
  { after: "Kettle" }, { before: /Elixir is installed/, after: /Elixir is installed/ });
await scenario("Kettle (a release build, FM-1_913) -> Elixir", { name: "Kettle", id: "FM-1_913" }, {},
  { before: /Elixir is installed/, after: /Elixir is installed/ });
await scenario("Windows' port name (\"2- Kettle\") -> Elixir", { name: "2- Kettle", id: "FM-1_900" },
  { after: "2- Elixir" }, { before: /Elixir is installed/, after: /Elixir is installed/ });
await scenario("Kettle -> Elixir, the cable pulled during the write, then resumed", { name: "Kettle", id: "FM-1_900" },
  { cut: 5 }, { before: /Elixir is installed/, after: /Elixir is installed/, cutOff: true });
await scenario("Elixir -> Elixir (an update)", { name: "Elixir", id: "FM-1_900" }, {},
  { before: /Elixir is installed/, after: /Elixir is installed/ });
await scenario("official V15 -> Elixir", { name: "FM-1", id: "FM-1_015" }, {},
  { before: /official V15/, after: /Elixir is installed/ });

console.log(failed ? `install_test: ${failed} FAILED` : "install_test: ok");
process.exit(failed ? 1 : 0);
