// SPDX-License-Identifier: GPL-3.0-only
// Kettle library test: web/library.js against the device's own code (tests/library_bridge.c, the emulator's C):
// info, kit import from LXR-02 .SND files, kit export, backup (read), restore (write) into another slot, load, and
// the device's refusals (a damaged image, data out of order, an abandoned write).
//   cc -O1 -std=gnu99 -w -I build/gen -I firmware/src -I firmware/hal tests/library_bridge.c -o build/library_bridge
//   node tests/library_test.mjs [LXR_KIT_DIR]      (default ../lxr-02_kits/LXR factory SD image 06.07/PROJ00)
import { spawn } from "node:child_process";
import { createInterface } from "node:readline";
import { readdirSync, readFileSync, existsSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";
import { Library, checkImage, packBackup, unpackBackup, sndFiles, sndBytes, message } from "../web/library.js";

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
check(info.proto === 1 && info.nlog === 5 && info.nkits === 64 && info.kitLen === 371, "info");

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
  check(k.name === "TESTKIT" && k.data.length === 371, "a kit in and out");
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

dev.stdin.end();
console.log(fails ? `library_test: ${fails} FAILED` : "library_test: ok");
process.exit(fails ? 1 : 0);
