# Putting an FM-1 firmware on The Firmware Hub – instructions for an AI assistant

You are helping a firmware maker list their M-VAVE FM-1 (or SMK-37) firmware on **The Firmware Hub**
(https://thefirmwarehub.com) and keep its card up to date. The maker may not be a programmer. Do the work for them where
you can, explain each step in plain words, and check everything against the rules below before they push.

## 1. What the hub needs before it lists a firmware

All six are required:

1. **Open source.** GPL-3.0, MPL-2.0 or another open licence, with the complete source public in the GitHub repository
   (a zip of the source is not enough). A `LICENSE` file in the repository root.
2. **A release on GitHub** with the firmware package (`.fwsc`) attached as a release asset. The hub picks up every new
   release by itself within three hours. A version with *beta*, *alpha*, *rc* or a 0.x number gets a beta tag.
3. **Its own identity:** an `FM-1_<number>` product name no other firmware uses. Check it at
   https://thefirmwarehub.com/submit/#check. Do not keep the number of the firmware it started from (Felucca's
   `FM-1_900` / `FM-1_980` …). Long numbers with a part of your own are safest, e.g. `FM-1_47` + version digits.
4. **The update loader** in the package, so players can install another firmware afterwards:
   https://thefirmwarehub.com/kb/#loader
5. **Tested on a real FM-1:** installed from the stock V15 firmware and from another firmware, and from this firmware
   another one installs again. Untested firmwares are not listed. If it cannot read other firmwares' projects or
   samples, the README says so.
6. **An `fmhub.json`** in the repository root, on the default branch (section 2). **No file, no listing.**

## 2. fmhub.json – the card, written by the maker

The hub reads this file every three hours and puts it on the firmware's card by itself, no mail, no approval. Changing
the card later = editing this file and pushing.

```json
{
  "what": "A six-track groovebox: TR drums, a sampler with a mic input, a step sequencer and song mode.",
  "author": "Your Name",
  "kinds": ["groove"],
  "play": "https://you.github.io/yourfirmware/play/",
  "editor": "https://you.github.io/yourfirmware/editor/",
  "cheat": "https://you.github.io/yourfirmware/cheatsheet.pdf",
  "image": "docs/card.png"
}
```

Only these seven fields are allowed – any other field makes the whole file unused. All of them are optional, but give at
least `what`, `kinds` and `image`.

| Field | Rule |
|---|---|
| `what` | The card's text. **English**, plain words: no HTML, no code, no links or web addresses (no `example.com`). At least 20 characters; more than 300 are cut at a word, so aim for 150–280. Say what it is and what you can do with it, for this version – not plans. **No advertising** (prices, discounts, "buy", sponsors, donation links) and **nothing hostile against anyone** – such a text is refused. |
| `author` | The maker's name as the card shows it under the title (without it: the name the hub wrote down at listing). Plain text, 2–60 characters, no links or web addresses, nothing hostile. |
| `kinds` | A list with one of `synth`, `groove` (groovebox), `drums` (drums and samplers), `chords`, `tracker`, `other`. If several are given, only the first counts. Pick the one people would look under. |
| `play` | `https://` address of the firmware running in a browser (an emulator), if there is one. |
| `editor` | `https://` address of a web editor for its sounds or settings, if there is one. |
| `cheat` | `https://` address of a cheat sheet: a web page or a PDF (e.g. on GitHub Pages). |
| `image` | The card's picture: a **path inside the repository** (not a URL), e.g. `docs/card.png`. See section 3. |

Valid JSON matters: double quotes, commas between fields, no comma after the last one, no comments.

## 3. The picture (`image`)

- **Square**, between **240 × 240 and 1024 × 1024 pixels**.
- **PNG, WebP or JPEG** (checked by the file's bytes, not its name). No SVG, no GIF.
- **300 KB at most.**
- Best: a capture of the FM-1's screen at exactly **240 × 240 as PNG** – a few KB, pixel-sharp (the hub scales it up
  without blurring). A photo of the device: square, 480–1024 px, as WebP or JPEG, well under 300 KB.
- Many firmwares have a browser emulator or a host build that can save its 240 × 240 screen – use that, don't take a
  phone photo of the screen.
- To change it later: replace the file, keep the path, push. The hub copies the picture and serves it itself.

## 4. Check before pushing

1. Validate the JSON (e.g. `python3 -m json.tool fmhub.json`).
2. Count the `what` text: ≤ 300 characters, English, no links, no `<` `>` `{` `}` or backticks.
3. Check the image: square, 240–1024 px, ≤ 300 KB, PNG/WebP/JPEG.
4. After pushing, the maker enters the repository at **https://thefirmwarehub.com/kb/#fmhub** ("Check your file"): it
   shows exactly what the card will show, or what is wrong.
5. On the hub, every card says under its text whether it comes from the maker's `fmhub.json`, and if the file or the
   picture is not used, why.

## 5. Submitting a new firmware

When all of section 1 is done, the maker sends one short mail to **zven@thefirmwarehub.com** (https://thefirmwarehub.com/submit/):

- the firmware's name and the maker's name as it should appear,
- the repository link,
- its `FM-1_` identity and its licence,
- that it was tested on a real FM-1 and leaving it again works.

Text, picture, cheat sheet and links do **not** go in the mail – they come from `fmhub.json`.

## 6. Afterwards

- New versions: just publish a GitHub release with the `.fwsc` attached. Same asset name and same identity is fine.
- Card text, picture, cheat sheet, browser version, editor: edit `fmhub.json` (and the image file), push, wait up to
  three hours. Never mail text changes.
- The maker's face on the Wall of Creators (https://thefirmwarehub.com/creators/) is their GitHub profile picture; a
  YouTube channel added to their GitHub profile's social accounts shows up there too.
