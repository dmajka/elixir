// SPDX-License-Identifier: GPL-3.0-only
// Elixir library test: web/library.js against the device's own code (tests/library_bridge.c, the emulator's C):
// info, kit import from LXR-02 .SND files, kit export, backup (read), restore (write) into another slot, load, and
// the device's refusals (a damaged image, data out of order, an abandoned write).
//   cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/library_bridge.c -o build/library_bridge
//   node tests/library_test.mjs [LXR_KIT_DIR]      (default ../lxr-02_kits/LXR factory SD image 06.07/PROJ00)
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { readdirSync, readFileSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { Library, checkImage, packBackup, unpackBackup, sndFiles, sndBytes, message, KL_KIT, KL_SOUND, packKitLib,
         unpackKitLib, parsePat, parseSng, songWindow, lxrProject, importLxrProject } from "../web/library.js";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const kitDir = process.argv[2] || join(root, "..", "lxr-02_kits", "LXR factory SD image 06.07", "PROJ00");
let fails = 0;
const check = (ok, what) => { if (!ok) { console.log("FAIL: " + what); fails++; } };

const dev = spawn(join(root, "build", "library_bridge"), [], { stdio: ["pipe", "pipe", "inherit"] });
const lines = createInterface({ input: dev.stdout });
const queue = [];
lines.on("line", (l) => queue.shift()(l));
const ask = (s) => new Promise((r) => { queue.push(r); dev.stdin.write(s + "\n"); });
const hex = (b) => Buffer.from(b).toString("hex");
const transport = {
  async request(wire) {
    const a = await ask(hex(wire));
    return a === "-" ? null : Uint8Array.from(Buffer.from(a, "hex"));
  },
};
const lib = new Library(transport);
const expectStatus = async (p, st, what) => {
  try { await p; check(false, what + ": accepted"); } catch (e) { check(e.status === st, what + ": " + e.message); }
};

// 1. info
let info = await lib.info();
console.log(`  info: proto ${info.proto}, ${info.version}, ${info.nlog} projects, ${info.nkits} kits of ${info.kitLen}`);
check(info.proto === 1 && info.nlog === 5 && info.nkits === 64 && info.kitLen === 398, "info");   // (0.16: 377 + 21 bytes)

// 2. LXR kits in
let kits = [];
if (existsSync(kitDir)) {
  const files = readdirSync(kitDir).map((n) => {
    const b = readFileSync(join(kitDir, n));
    return { name: n, webkitRelativePath: "", arrayBuffer: async () => b.buffer.slice(b.byteOffset, b.byteOffset + b.length) };
  });
  kits = await sndFiles(files);
  console.log(`  ${kits.length} LXR kits in ${kitDir.split("/").slice(-2).join("/")}: ${kits.slice(0, 4).map((k) => k.name).join(", ")} ..`);
  for (let i = 0; i < kits.length && i < 64; i++) await lib.kitPut(i, kits[i].name, kits[i].data, i === 3);
  const names = await lib.kitNames();
  check(kits.slice(0, 64).every((k, i) => names[i] === k.name.slice(0, 8).trimEnd()), "kit names after import");
  info = await lib.info();
  check(info.kitLoaded === 3, "KIT_PUT's load flag");
  // what came back: the LXR's bytes, but Out = FX BUS (6, parameters 215..220) becomes STEREO with FX SEND 127
  let same = 0, total = 0, fxbus = 0;
  for (let i = 0; i < Math.min(kits.length, 64); i++) {
    const k = await lib.kitGet(i);
    const back = sndBytes(k.name, k.data).subarray(8), orig = kits[i].data;
    for (let j = 0; j < orig.length; j++, total++) {
      const out = j >= 215 && j <= 220 && orig[j] >= 6;
      fxbus += out;
      same += back[j] === (out ? 0 : orig[j]);
    }
  }
  console.log(`  kits read back: ${same} of ${total} bytes as in the files (${fxbus} Out = FX BUS as sends)`);
  check(same === total, "kits read back as imported");
} else {
  console.log("  (no LXR kit folder: kit import from files skipped)");
  const data = new Uint8Array(247).fill(64);
  await lib.kitPut(5, "TESTKIT", data);
  const k = await lib.kitGet(5);
  check(k.name === "TESTKIT" && k.data.length === 377, "a kit in and out");
}

// 3. backup: the session saved into project 1, read back
await lib.saveProject(0, "BACKUP");
info = await lib.info();
check(info.slots[0].exists && info.slots[0].name === "BACKUP", "SAVE into project 1");
const img = await lib.readProject(0);
const c = checkImage(img);
console.log(`  backup: project 1 read, ${img.length} bytes, format ${c.format}, ${c.ok ? "intact" : c.why}`);
check(c.ok && c.name === "BACKUP", "the backup's image");

// 4. restore into project 3, renamed; the same payload; load it
const flashBefore = +(await ask("flash"));
await lib.writeProject(2, img, "RESTORED");
info = await lib.info();
check(info.slots[2].exists && info.slots[2].name === "RESTORED", "restored into project 3");
const img2 = await lib.readProject(2);
check(Buffer.compare(Buffer.from(img.subarray(256)), Buffer.from(img2.subarray(256))) === 0, "the restored payload");
console.log(`  restore: project 3 written (${+(await ask("flash")) - flashBefore} flash operations), payload identical`);
await lib.loadProject(2);
info = await lib.info();
check(info.loaded === 2, "LOAD the restored project");
const k3 = await lib.kitGet(0);
check(!kits.length || k3.name === kits[0].name.slice(0, 8).trimEnd(), "the restored project's kits");

// 5. a backup file, both ways
const back = unpackBackup(packBackup([{ slot: 0, img }, { slot: 2, img: img2 }]));
check(back.length === 2 && back[1].slot === 2 && checkImage(back[1].img).ok, "a backup file round trip");
check(unpackBackup(img)[0].slot === -1, "a single project as a backup file");

// 6. the device refuses: a damaged payload (its CRC), data before BEGIN, an abandoned write
const bad = img.slice();
bad[300] ^= 0x55;
await lib.call(0x20, [1, ...[bad.length & 255, (bad.length >> 8) & 255, 0, 0], 0, 0, 0, 0, 0, 0, 0, 0]);
for (let off = 0; off < bad.length; off += 256) {
  const n = Math.min(256, bad.length - off);
  await lib.call(0x21, [off & 255, (off >> 8) & 255, 0, 0, n & 255, n >> 8, ...bad.subarray(off, off + n)]);
}
await expectStatus(lib.call(0x22), 5, "a damaged image");
info = await lib.info();
check(!info.slots[1].exists, "nothing written for a damaged image");
await expectStatus(lib.call(0x21, [0, 0, 0, 0, 1, 0, 0]), 8, "data before BEGIN");
await lib.call(0x20, [1, ...[img.length & 255, (img.length >> 8) & 255, 0, 0], 0, 0, 0, 0, 0, 0, 0, 0]);
await expectStatus(lib.saveProject(3, "LOCKED"), 4, "SAVE while a restore is open");
await ask("run 10500");                              // (the write is given up after 10 s)
await lib.saveProject(3, "AFTER");
info = await lib.info();
check(info.slots[3].exists && info.slots[3].name === "AFTER", "SAVE after an abandoned restore");
check((await transport.request(message(0x01, 1, [5]).map((b, i, a) => (i === a.length - 2 ? b ^ 1 : b)))) === null,
      "a garbled message: no answer");

// 7. 0.8: the kit library: kits and sounds in, listed, read back, deleted; a backup file; the power-on scan
{
  const kit = new Uint8Array(377).map((_, i) => (i * 7) & 127);
  await lib.libPut(KL_KIT, 7, "LIBKIT", kit);
  await lib.libPut(KL_KIT, 120, "OTHER", kit.subarray(0, 247));
  const snd = Uint8Array.from([0, 1, 4, 10, 20, 30, 40]);
  await lib.libPut(KL_SOUND, 3, "SNARE?", snd);
  let lk = await lib.libList(KL_KIT), ls = await lib.libList(KL_SOUND);
  check(lk.length === 2 && lk[0].slot === 7 && lk[0].name === "LIBKIT" && lk[1].slot === 120 && ls.length === 1,
        "the kit library listed");
  const back7 = await lib.libGet(KL_KIT, 7);
  check(Buffer.compare(Buffer.from(back7.data), Buffer.from(kit)) === 0, "a library kit read back");
  const entries = await lib.libBackup();
  const file = packKitLib(entries), again = unpackKitLib(file);
  check(again.length === 3 && again[2].kind === KL_SOUND && again[2].data.length === 7, "a kit library backup file");
  await lib.libDelete(KL_KIT, 7);
  await lib.libDelete(KL_KIT, 120);
  await lib.libDelete(KL_SOUND, 3);
  check((await lib.libList(KL_KIT)).length === 0, "library kits deleted");
  await expectStatus(lib.libGet(KL_KIT, 7), 7, "a deleted kit");
  await lib.libRestore(again);
  await ask("boot");
  lk = await lib.libList(KL_KIT);
  ls = await lib.libList(KL_SOUND);
  check(lk.length === 2 && ls.length === 1 && (await lib.libGet(KL_SOUND, 3)).data[2] === 4,
        "the kit library restored, at power-on");
  await expectStatus(lib.libPut(KL_SOUND, 9, "BAD", [9, 0, 0]), 2, "a sound of no kind");
  console.log(`  kit library: ${lk.length} kits, ${ls.length} sound, backup file ${file.length} bytes`);
}

// 8. 0.8: an LXR-02 project: its kits by number, its patterns, its songs
{
  const projDir = existsSync(kitDir) ? kitDir : null;
  const lxrFiles = projDir ? readdirSync(projDir).map((n) => {
    const b = readFileSync(join(projDir, n));
    return { name: n, webkitRelativePath: "", arrayBuffer: async () => b.buffer.slice(b.byteOffset, b.byteOffset + b.length) };
  }) : [];
  // a made-up V3 pattern, and V2 / V1 forms of it
  const mk = (v) => {
    const head = v === 1 ? 8 : 14, n = head + 7 * (64 * 7 + (v === 3 ? 64 : 0)) + 56 + 1 + 7 + (v === 1 ? 0 : 1);
    const b = new Uint8Array(n);
    b.set([...Buffer.from("PATNAME ")], 0);
    if (v > 1) b.set([...Buffer.from("PatV0" + v)], 8);
    let i = head;
    for (let t = 0; t < 7; t++) {
      for (let s = 0; s < 64; s++, i += 7) b.set([s % 4 === 0 ? 0x80 | 100 : 100, 127, 60 + t, 255, 0, 255, 0], i);
      if (v === 3) { for (let s = 0; s < 64; s++) b[i + s] = 0x70 | (s === 4 ? 2 : 0); i += 64; }
    }
    for (let t = 0; t < 7; t++) for (let k = 0; k < 4; k++, i += 2) { b[i] = t === 0 ? 0x11 : 0; b[i + 1] = k; }
    b[i++] = 9;
    for (let t = 0; t < 7; t++) b[i++] = t === 2 ? 32 : 16;
    if (v > 1) b[i] = 5;
    return b;
  };
  const p3 = parsePat(mk(3), "07-.PAT"), p2 = parsePat(mk(2)), p1 = parsePat(mk(1));
  check(p3.version === 3 && p3.number === 7 && p3.kit === 5 && p3.len[2] === 32 && p3.shuffle === 9 && p3.timing[0][4] === 0x72 &&
        p2.version === 2 && p2.timing[0][4] === 0x70 && p1.version === 1 && p1.kit === 0 && p1.main[0][3] === 0x311,
        "an LXR pattern file, its three versions");
  await lib.patPut(4, p3);
  const d = Buffer.from(await ask("pat 4"), "hex");
  let ok = true, i = 0;
  for (let t = 0; t < 7; t++) {
    ok &&= Buffer.compare(d.subarray(i, i + 448), Buffer.from(p3.steps[t])) === 0;
    i += 448;
    ok &&= Buffer.compare(d.subarray(i, i + 64), Buffer.from(p3.timing[t])) === 0;
    i += 64;
  }
  ok &&= d[i] === 0x11 && d[i + 7] === 3 && d[i + 56] === 9 && d[i + 57 + 2] === 32 && d[i + 64] === 5;
  check(ok, "an LXR pattern into Elixir's pattern 5, as in the file");
  check(JSON.stringify(songWindow([0, 3, 40, 33, 64, 65], 32)) === JSON.stringify([31, 31, 8, 1, 64, 65]) &&
        JSON.stringify(songWindow([0, 3, 40, 64], 0)) === JSON.stringify([0, 3, 31, 64]), "a song's patterns, the half picked");
  await lib.songPut(2, [0, 1, 1, 2, 65, ...new Array(59).fill(64)]);
  check((await ask("song 2")).startsWith("0001010241"), "a song in");
  // a whole SD card (two projects' folders): one project only; a kit import takes both, folder by folder
  {
    const f = (path) => ({ name: path.split("/").pop(), webkitRelativePath: path,
                           arrayBuffer: async () => new Uint8Array(8 + 247).buffer });
    const card = [f("SD/PROJ01/00-B.SND"), f("SD/PROJ00/01-A.SND"), f("SD/PROJ00/00-A.SND"), f("SD/PROJ00/00-X.PAT"), f("SD/README.TXT")];
    let why = "";
    try { await lxrProject(card); } catch (e) { why = e.message; }
    check(/2 LXR-02 projects/.test(why), "an LXR project: a whole card refused");
    const ks = await sndFiles(card, { skipInit: false });
    check(ks.map((k) => k.file).join() === "SD/PROJ00/00-A.SND,SD/PROJ00/01-A.SND,SD/PROJ01/00-B.SND",
          "kits from a folder: its subfolders' too, in order, nothing else");
  }
  if (lxrFiles.length) {
    const proj = await lxrProject(lxrFiles);
    const r = await importLxrProject(lib, proj, 0);
    const pats = proj.pats.filter((p) => p.number < 32);
    let same = 0;
    for (const p of pats) {
      const q = Buffer.from(await ask("pat " + p.number), "hex");
      same += Buffer.compare(q.subarray(0, 448), Buffer.from(p.steps[0])) === 0 && q[q.length - 1] === p.kit;
    }
    console.log(`  LXR project: ${r.kits} kits, ${r.pats} patterns (${proj.pats.map((p) => "V" + p.version).filter((v, k, a) => a.indexOf(v) === k)}), ${r.songs} songs; ${same} patterns as in their files`);
    check(r.pats === pats.length && same === pats.length && (await lib.kitNames())[proj.kits[1].number] === proj.kits[1].name.slice(0, 8).trimEnd(),
          "an LXR project in: kits by number, patterns as in the files");
  }
}

dev.stdin.end();
console.log(fails ? `library_test: ${fails} FAILED` : "library_test: ok");
process.exit(fails ? 1 : 0);
