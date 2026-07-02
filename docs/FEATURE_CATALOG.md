# DAW Feature Catalog — The Full Landscape

*Companion to the Architecture Plan. That doc covers **how to build** and in **what order**;
this one is the complete **inventory of what exists** in a feature-rich DAW, so you can
decide how far to take Forge.*

---

## How to read this

**Tiers** (in the Tier column):
- **Core** — table stakes. Any DAW you'd actually use needs these.
- **Standard** — expected in something people call "feature-rich."
- **Pro** — advanced, specialized, or a heavy lift. Differentiators, not requirements.

**What the foundation already covers:** building on Tracktion Engine hands you a large chunk
of the **Core** backbone for free — the audio engine, recording, MIDI handling, mixing
structure, automation system, and plugin hosting. The real build effort concentrates in the
**editing tools, workflow/UI, and built-in content** — mostly the Standard and Pro rows.

**Reality check:** no single DAW has every feature below. They specialize. "Feature-rich"
means *deep where it counts for your use*, not "all of it."

---

## 1. Audio Engine & Performance

| Feature | Tier | Note |
|---|---|---|
| Multi-track audio playback/record | Core | The foundation. |
| Configurable sample rate (44.1k–192k+) | Core | |
| Bit depth options (16/24/32-float) | Core | |
| 64-bit float internal processing | Standard | Headroom; avoids cumulative rounding. |
| Driver support (Core Audio / ASIO / WASAPI) | Core | JUCE handles this. |
| Low-latency monitoring | Core | |
| Multi-core / multi-threaded processing | Standard | Tracktion's graph does this. |
| Plugin Delay Compensation (PDC) | Standard | Keeps everything time-aligned. |
| Variable buffer sizes | Core | Latency vs. CPU tradeoff. |
| Track freeze / bounce-in-place | Standard | Frees CPU on heavy tracks. |
| Disk streaming for large projects | Standard | Plays big sessions without loading all to RAM. |
| Aggregate / multiple audio devices | Pro | Combine interfaces. |
| CPU/performance meter | Standard | |

## 2. Recording

| Feature | Tier | Note |
|---|---|---|
| Multitrack simultaneous recording | Core | |
| Input monitoring (direct + software) | Core | |
| Metronome / click (customizable) | Core | |
| Count-in / pre-roll | Core | |
| Punch in/out (manual + auto) | Standard | Re-record a section hands-free. |
| Loop recording into take lanes | Standard | Feeds comping. |
| Overdubbing | Core | |
| Step recording (MIDI) | Standard | Enter notes without playing in time. |
| Take/lane management | Standard | |
| Record arm indicators / safe modes | Core | |

## 3. MIDI Sequencing & Editing

| Feature | Tier | Note |
|---|---|---|
| MIDI record/playback | Core | |
| Piano roll editor | Core | The main MIDI workspace. |
| Quantize (grid, swing, groove) | Core | |
| Velocity editing | Core | |
| MIDI CC editing / lanes | Standard | |
| Step sequencer | Standard | |
| Drum editor / drum grid | Standard | |
| MIDI effects (arpeggiator, chord, scale) | Standard | |
| Transpose / humanize / legato tools | Standard | |
| Note repeat / chord trigger | Standard | |
| MPE support (poly expression) | Pro | For expressive controllers. |
| MIDI learn / controller mapping | Standard | Map hardware knobs to params. |
| MIDI clock / sync in-out | Standard | |
| Score / notation editor | Pro | Composition-focused DAWs. |

## 4. Audio Editing & Manipulation

| Feature | Tier | Note |
|---|---|---|
| Non-destructive editing | Core | Edit a view, never the file. |
| Cut / copy / paste / split / trim | Core | |
| Fades + crossfades (multiple curves) | Core | |
| Clip gain / gain envelopes | Standard | |
| Time-stretching (multiple algorithms) | Standard | Stretch without changing pitch. |
| Pitch-shifting | Standard | |
| Warp markers / elastic audio / flex time | Pro | Bend audio to the grid. |
| Audio quantize (transient-based) | Pro | Tighten a sloppy take. |
| Comping (swipe takes into one) | Standard | |
| Reverse / normalize / silence | Standard | |
| Transient detection / slicing | Pro | |
| Slice-to-MIDI / slice-to-sampler | Pro | |
| Built-in pitch correction | Pro | Tune-style. |
| Audio-to-MIDI conversion | Pro | Extract notes from audio. |
| Spectral editing | Pro | Edit in the frequency domain. |
| Restoration (de-noise, de-click) | Pro | |

## 5. Mixing & Routing

| Feature | Tier | Note |
|---|---|---|
| Per-track channel strip (gain/pan/phase) | Core | |
| Mute / solo (+ solo-safe, AFL/PFL) | Core | |
| Insert effect chains | Core | |
| Buses / groups / submixes | Standard | |
| Sends / returns (pre/post fader) | Standard | Share one reverb across tracks. |
| Aux channels | Standard | |
| VCA / group faders | Pro | Control many faders as one. |
| Sidechain routing | Standard | Feed one signal into another's processor. |
| External hardware insert | Pro | Route out to outboard gear and back. |
| Flexible any-to-any routing | Pro | |
| Metering suite (peak/RMS/LUFS/true-peak) | Standard | |
| Goniometer / correlation / spectrum | Pro | Visual mix analysis. |
| Surround / immersive (Dolby Atmos) | Pro | Post & film. |
| Master bus processing | Core | |
| Track folders / groups | Standard | Organize big sessions. |

## 6. Automation & Modulation

| Feature | Tier | Note |
|---|---|---|
| Volume / pan automation | Core | |
| Automation for any plugin parameter | Standard | |
| Automation modes (read/write/touch/latch/trim) | Standard | |
| Automation curves (linear/bezier/stepped) | Standard | |
| Multiple automation lanes per track | Standard | |
| Modulators (LFOs, envelopes → params) | Pro | Bitwig-style modulation. |
| Macro controls | Standard | One knob drives many params. |
| Parameter linking | Pro | |

## 7. Plugins, Instruments & Effects

| Feature | Tier | Note |
|---|---|---|
| Host VST3 / AU | Core | Your headline requirement. |
| Host CLAP | Standard | Newer open format; worth supporting. |
| Plugin scanning + blacklisting | Core | |
| Out-of-process plugin sandboxing | Standard | A crash can't kill the DAW. |
| Plugin preset management | Standard | |
| Plugin chains / racks / containers | Standard | |
| Multi-out instruments | Standard | Drum plugin → separate channels. |
| Built-in EQ | Core | |
| Built-in dynamics (comp/limiter/gate) | Core | |
| Built-in reverb / delay | Core | |
| Built-in modulation / distortion / saturation | Standard | |
| Built-in synth(s) | Standard | Seeded by any synth DSP you port in. |
| Built-in sampler / drum machine | Standard | |
| ARA support (Melodyne-style) | Pro | Deep audio-plugin integration. |

## 8. Arrangement & Workflow

| Feature | Tier | Note |
|---|---|---|
| Timeline / arrangement view | Core | |
| Snap-to-grid + grid resolution | Core | |
| Zoom (H/V, zoom-to-selection) | Core | |
| Markers / cue points / regions | Standard | Jump between song sections. |
| Arranger / song-section track | Standard | Reorder verse/chorus blocks. |
| Tempo track (tempo changes) | Standard | |
| Time-signature changes | Standard | |
| Multiple time formats (bars/timecode/samples) | Standard | |
| Track templates / project templates | Standard | |
| Drag-and-drop from browser | Standard | |
| Loop / clip library | Standard | |

## 9. Clip Launching & Live Performance

> **This is now Forge's PRIMARY identity** (direction reset 2026-06-30 — see [DIRECTION.md](DIRECTION.md)).
> The Session clip grid is the *primary* surface, played from grid controllers (Launchpad / APC40 mkII).
> The tiers below are updated accordingly.

| Feature | Tier | Note |
|---|---|---|
| Session / clip-launch view | **Core (PRIMARY surface)** | Ableton's nonlinear grid — *the* main surface (`SessionView`). |
| Scenes | **Core** | Fire a row of clips together. |
| Grid-controller (Launchpad / APC40) integration | **Core — driver built (W02); HW pending** | Forge-native driver + a Launchpad driver shipped on the `ControlSurface` seam (gate `--selftest-controlsurface`); on-screen grid works without it. Remaining: on-hardware byte-mapping + an APC40 mkII driver. |
| Key / MIDI mapping for live control | **Core** | MIDI-learn (`MidiLearn`) + MIDI-clock / Ableton Link sync. |
| Follow actions | Standard | Clips trigger the next automatically. |
| Live looping | Pro | |
| Crossfader | Pro | DJ-style blending. |

> *Decide early if Forge wants this. It's a whole second workflow — powerful for
> electronic/live use, irrelevant for straight recording. Many great DAWs skip it entirely.*

## 10. Browser & Content Management

| Feature | Tier | Note |
|---|---|---|
| File / sample browser | Core | |
| Tempo-synced sample preview | Standard | Hear loops in time before dropping. |
| Tagging / search / favorites | Standard | |
| Preset browser | Standard | |
| Bundled sample/loop content | Standard | |
| Cloud / shared library | Pro | |

## 11. Tempo, Sync & Timing

| Feature | Tier | Note |
|---|---|---|
| Tap tempo | Standard | |
| Tempo detection from audio | Pro | |
| Tempo mapping to a recording | Pro | Follow a live performance's tempo. |
| Groove templates / extraction | Pro | |
| Ableton Link | Standard | Sync to other apps/devices. |
| MIDI Clock / MTC | Standard | |

## 12. Export, Bounce & Delivery

| Feature | Tier | Note |
|---|---|---|
| Mixdown to WAV/AIFF/FLAC | Core | |
| Export to MP3 / AAC | Core | |
| Stem / per-track export (batch) | Standard | |
| Export selected region / loop | Standard | |
| Offline (faster-than-realtime) bounce | Standard | |
| Dithering options | Standard | |
| Sample-rate / bit-depth conversion on export | Standard | |
| Loudness normalization to streaming targets | Pro | -14 LUFS etc. |
| MIDI file export | Standard | |
| Consolidate / flatten / archive project | Standard | Collect all files in one place. |

## 13. Notation & Scoring

| Feature | Tier | Note |
|---|---|---|
| Score editor | Pro | |
| Sheet-music print / export | Pro | |
| Lead sheets / chord symbols | Pro | |
| *Mostly relevant only if Forge leans composition.* | | |

## 14. Video & Post

| Feature | Tier | Note |
|---|---|---|
| Video track / playback | Pro | |
| Frame-accurate sync to picture | Pro | |
| Markers-to-picture for scoring | Pro | |
| *Skip unless you're scoring to video.* | | |

## 15. Project & Session Management

| Feature | Tier | Note |
|---|---|---|
| Save / load projects | Core | |
| Auto-save / auto-backup | Core | Don't ship without this. |
| Crash recovery | Standard | |
| Project versions / snapshots | Standard | "Save as new version." |
| Recent projects list | Core | |
| Undo/redo (multi-level + history) | Core — **shipped (W05)** | Global Edit ▸ Undo/Redo (Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y) over the Edit's UndoManager, live menu enablement + cross-surface refresh; gate `--selftest-undo`. Built on Tracktion's model. |
| Project consolidation (collect files) | Standard | |

## 16. UI, UX & Hardware Control

| Feature | Tier | Note |
|---|---|---|
| Customizable keyboard shortcuts | Standard | |
| Multi-monitor / detachable windows | Standard | |
| HiDPI / scalable UI | Core | |
| Track coloring | Standard | |
| Themes / color schemes | Pro | |
| Customizable layouts / workspaces | Standard | |
| Control-surface integration (Mackie/HUI) | Pro | Physical mixer control. |
| Tooltips / contextual help | Standard | |
| Touchscreen support | Pro | |

## 17. Collaboration & Cloud

| Feature | Tier | Note |
|---|---|---|
| Cloud project storage | Pro | |
| Version history | Pro | |
| Collaborative / shared sessions | Pro | |
| *Big lift; rarely worth it for a personal tool.* | | |

## 18. AI & Emerging

| Feature | Tier | Note |
|---|---|---|
| Stem separation (split a mix into parts) | Pro | Increasingly common. |
| Assisted mastering | Pro | |
| Generative / assistive composition | Pro | |
| Smart tempo / smart quantize | Pro | |
| *Optional flavor — not what makes a DAW solid.* | | |

---

## What "feature-rich" actually means

No DAW has all of the above — they pick an identity and go deep there:
- **Ableton Live** — live performance + electronic; the clip-launch session view is its soul.
- **Pro Tools** — recording, editing, and post/film; the studio standard for tracking.
- **Logic Pro** — composition and production; huge bundled instrument/content library.
- **Cubase / Nuendo** — deep MIDI + scoring (Cubase) and post/immersive (Nuendo).
- **Reaper** — endlessly customizable and scriptable; light, cheap, do-anything.
- **FL Studio** — pattern/step-sequencing workflow, beloved for beats.
- **Studio One** — drag-and-drop modern workflow, mastering built in.

**The takeaway for Forge (updated 2026-06-30):** "feature-rich" is a direction, not a checklist. Forge's
center of gravity is **live clip-launching** — a **sample / scene-based Session grid** played from grid
controllers (Launchpad / APC40 mkII); strong MIDI, a good built-in instrument set, and the mixer support
it, with the **linear arrangement view secondary**. *(Earlier this doc framed it as "recording +
arrangement first … live clip-launching can wait" — that has been **reversed**; see
[DIRECTION.md](DIRECTION.md).)* Video/post, scoring, and cloud collab can still wait.
