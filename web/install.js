// SPDX-License-Identifier: GPL-3.0-only
// Elixir's install page: what the device check says (0.14). web/make_site.py inlines it after fm1pkg.js and
// fm1ota.js; tests/install_test.mjs runs it against simulated FM-1s.
//
// The FM-1 answers the same identity (FM-1_9xx) whatever alternative firmware runs, so the port's name tells them
// apart: Elixir names its port "Elixir" (a computer may keep showing an older name for it after an update, which
// is Elixir too); Felucca's is "Felucca …".

// the firmware a port's name says: "elixir", "felucca" or null
export function firmwareOf(portName) {
  const m = /elixir|kettle|felucca/i.exec(portName || "");
  return m ? (/felucca/i.test(m[0]) ? "felucca" : "elixir") : null;
}

// dev: { id: { text, model, version }, name } as Updater.find returns it
export function describe(dev) {
  const id = dev.id, fw = firmwareOf(dev.name);
  if (id.model.toLowerCase().startsWith("ota-"))
    return "In update mode: an install was cut off. Press Install Elixir (or Install official V15) to finish it.";
  if (id.model !== "FM-1") return `${id.text}: not an FM-1.`;
  if (id.version >= 900 && fw === "elixir")
    return `${id.text}: Elixir is installed. Install Elixir (step 3) to update or reinstall it; your projects are kept.`;
  if (id.version >= 900 && fw === "felucca")
    return `${id.text}: Felucca is installed. Back up Felucca's data on its own page first, then install V15 ` +
      "(step 2) if you haven't done so before, or install Elixir (step 3).";
  if (id.version === 15) return `${id.text}: official V15. Ready for Elixir: go to step 3.`;
  if (id.version >= 900) return `${id.text}: an alternative firmware (Elixir, Felucca or another). Elixir: ` +
    "install Elixir (step 3) to update it. Another: install V15 (step 2) first if you haven't before.";
  return `${id.text}: an official version other than V15. Install V15 (step 2) first.`;
}
