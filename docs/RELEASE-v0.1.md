# Guitar Companion v0.1 — first public beta

A companion for the guitarist: a neural amp sim built around the idea of having
something **to play with**. Amp and cabinet from real captures, a drummer you
can write grooves for, an arrangement that follows you through a song, and a rig
that changes on its own when the chorus arrives.

## Install

Download **`Guitar-Companion-0.1-win64-setup.exe`** and run it.

- Installs the **standalone application** and, optionally, the **VST3** into
  `C:\Program Files\Common Files\VST3`.
- Needs **Windows 10 or 11, 64-bit**.
- Uninstall from Add/Remove Programs.

### The embedded browser needs Windows 11 (or the WebView2 Runtime)

The Tone Store opens TONE3000's own pages **inside the app**, using Microsoft's
WebView2. **Windows 11 ships with it.** On Windows 10 it usually arrives with
Edge, but not always — if it is missing the installer tells you, and the store
falls back to opening TONE3000 in your normal browser. Nothing breaks either
way; the difference is only whether the pages appear inside the window. To get
the embedded version on Windows 10, install the free
[WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/).

Everything else in the program works the same on Windows 10 and 11.

## What is in it

- **Amp + cab** — NAM captures (A1 and A2), up to 3 parallel AMP+CAB rigs, IR
  convolution with low/high cut and phase, automatic resampler.
- **23 effects**, reorderable by drag-and-drop, with variations documented
  per effect, plus a slot that hosts any third-party VST3.
- **Tone Store** — sign in to TONE3000 and browse the whole catalogue without
  leaving the app; downloads, offline library, and a catalogue of free VST3
  effects that installs without admin rights.
- **Drums** — sequencer with real samples or a hosted drum VST3, editable
  notation, ~157 grooves and ~53 fills across 14 genres, groove generator.
- **Song / Scenes** — up to 8 sections, each with its own bars and its own rig
  snapshot, switched on the bar line while you play.
- **Stage** — a live screen with big targets and the next scene in view.
- **Recording** — a take as a mix plus separate guitar and drum stems.

## This is a beta

It works and it is used daily on a real rig, but v0.1 is a first public release:
expect rough edges and cases nobody has hit yet. **Keep backups of anything that
matters** — your presets are in `Documents\Guitar Companion\Presets`.

Please open an issue when something goes wrong, with what you did, what you
expected and what happened.

### Known limitations

- **Windows only.** Nothing here is portable yet.
- TONE3000 is used on its **free, non-commercial API tier**, so browsing happens
  through their own pages. Their filters take **one value at a time**, which is
  why the browser panel has NAM/IR and A1/A2 switches instead of showing
  everything at once.
- Tones published in **AIDA-X, Proteus or Amped Roots** formats cannot be
  loaded; they are labelled as such rather than downloaded.
- You need **your own TONE3000 API key** (free, from their Settings → API Keys).
  No credential ships with this program.

## Upgrading from an earlier build

If you used this under its older names (*PedalForge NAM* or *GuitarRig NAM*),
your captures, IRs, presets and drum bars are moved to
`Documents\Guitar Companion` the first time you start it. Nothing is deleted; if
a file cannot be moved it is copied and the original stays where it was. **If
you had the plugin loaded in a DAW project, you will need to add it again** —
the plugin's internal id changed with the rename, and doing that now rather than
after v0.1 keeps it from happening to everyone later.

## Licence and credits

AGPLv3. This program is built on other people's open-source work — the neural
engine, the framework, the DSP it learned from, the drum samples, the music
font. Every one of them is credited by name in
[THIRD_PARTY.md](../THIRD_PARTY.md) and, per effect, in
[docs/EFEITOS.md](EFEITOS.md).

Not affiliated with, sponsored by, or endorsed by TONE3000.
