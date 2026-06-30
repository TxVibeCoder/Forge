# DAW Architecture Plan — Native Desktop Build
*Working title: **Forge** (rename whenever you like)*
*Status: Draft v1 — for review. Persisted into the repo as the source of truth.*

> **Product direction (2026-06-30):** Forge is a **sample / scene-based, controller-driven DAW** (Session
> clip grid primary, Arrange secondary). The authoritative brief is **[DIRECTION.md](DIRECTION.md)**. This
> engine plan is largely direction-agnostic, but where it implies arrangement-first, DIRECTION.md wins.

> **Phase 0 research addenda (2026-06):** Two claims in this doc were corrected during
> toolchain research — see the **Addenda** section at the bottom before relying on
> `rtcheck` or ASIO. Everything else verified current.

---
## 1. Locked decisions
These two choices drive everything below. Changing either one changes the document.

| Decision | Choice | Consequence |
|---|---|---|
| **Platform** | Native desktop | Lowest latency, full hardware + file access, can host the world's plugins. Biggest build. |
| **Plugin hosting** | Load VST3 (all OSes) + AU (macOS) | This is the line between a real DAW and a toy. It also forces the stack — see §3. |
| **Audience** | You + a few friends (non-commercial) | The entire stack stays **free** under open-source licensing. Revisit licensing only if you ever sell it (§11). |

**Targets:** macOS + Windows first. Linux is a low-cost add later (VST3 works there; AU does not exist outside macOS).

---
## 2. Design principles
Four rules that this whole architecture exists to protect. When a decision is unclear later, fall back to these.

1. **The audio thread is sacred.** It never allocates memory, never locks, never blocks, never touches a file. Break this rule and you get clicks, pops, and dropouts — the one thing users never forgive. Everything else bends around this.
2. **The model is the single source of truth.** The project (tracks, clips, plugins, automation, tempo) lives in one data structure. The UI and the audio engine are both *readers* of it. Nothing is "stored" in a UI widget.
3. **The UI observes; it never reaches into the audio.** User actions become *commands* that change the model. The audio engine picks up the change on its next block. The UI never pokes the audio thread directly.
4. **Build on a proven core; spend your energy where it's yours.** The real-time audio graph is one of the hardest things in all of software. Don't rebuild it. Stand on a battle-tested engine and pour your effort into the workflow, the UI, and your own instruments/effects — the parts that make it *yours*.

---
## 3. Recommended stack

| Layer | Choice | Why this one |
|---|---|---|
| **Language** | C++20 | Required by the engine below, and the native audio world is overwhelmingly C++. |
| **App framework** | **JUCE** | The de-facto standard for audio apps. Gives you audio/MIDI device I/O, **VST3/AU plugin hosting**, GUI components, and DSP utilities in one place. Most commercial plugins and many DAWs are built on it. |
| **DAW engine** | **Tracktion Engine** | The production engine under the Waveform DAW. Free under GPL for your non-commercial use. Hands you the spine for free: the real-time audio graph, the project ("Edit") model, transport, recording, automation, and plugin integration. |
| **Build system** | CMake | What both JUCE and Tracktion Engine target. One config builds Mac + Windows. |
| **Plugin formats** | VST3 + AU | VST3 is cross-platform; AU is macOS-only. Skip legacy VST2 (licensing is closed). |

> **Note on the learning curve:** if you're coming from a higher-level language (Python/JavaScript), C++ is the real curve here — manual memory, build complexity, stricter types. The upside is that JUCE + Tracktion are heavily documented and very AI-assistable, and the core concepts (real-time audio scheduling, systems design) transfer directly. The syntax is the tax; the architecture is learnable.

---
## 4. The one strategic call: build *on* Tracktion vs. *from scratch*
**Recommendation: build ON Tracktion Engine.** Reasoning:

- It gives you the hardest 60% — the lock-free, sample-accurate, multi-threaded audio graph plus plugin hosting and a serializable project model — already written and proven in a shipping product.
- It's free for your use (GPL), which matches the "without buying one" goal.
- You still build everything that makes it *feel* like your DAW: the entire UI, the workflow, your own instruments and effects. You will write plenty from scratch — just not the part that's pure, low-glory pain.
- It hosts VST3/AU out of the box, which is your headline requirement.

You're a from-scratch builder by instinct, so the from-scratch path is documented fully in **Appendix A** — including exactly what extra you'd take on and when it's the right call. Short version: choose from-scratch only if *building the engine itself* is the goal, not if *a working DAW* is the goal.

The rest of this document assumes the **build-on-Tracktion** path.

---
## 5. System architecture (layers)

```
┌─────────────────────────────────────────────────────────────┐
│  UI LAYER  (JUCE Components — runs on the message thread)     │
│  Arrange view · Mixer · Piano roll · Clip editor ·           │
│  Plugin windows · Transport bar · Browser                    │
└───────────────┬──────────────────────────┬──────────────────┘
                │ commands (edits)          │ observes (read-only)
                ▼                           │
┌─────────────────────────────────────────────────────────────┐
│  SERVICES LAYER  (non-real-time machinery)                   │
│  Device mgr · Plugin scan/host · File mgr · Undo/redo ·      │
│  Transport control · Render/export · Waveform thumbnails     │
└───────────────┬─────────────────────────────────────────────┘
                │ reads / mutates
                ▼
┌─────────────────────────────────────────────────────────────┐
│  MODEL LAYER  — "the Edit"  (single source of truth)         │
│  Tracks · Clips · Plugins-on-tracks · Automation ·           │
│  Tempo/time-sig · Markers   →  serialized to project file    │
└───────────────┬─────────────────────────────────────────────┘
                │ compiles into ▼
┌─────────────────────────────────────────────────────────────┐
│  AUDIO ENGINE LAYER  (REAL-TIME · lock-free · multi-thread)  │
│  Tracktion's processing graph: clip nodes → plugin nodes →   │
│  mixer nodes → master.  Plugin Delay Compensation here.      │
│  ── NOTHING in this layer allocates, locks, or blocks ──     │
└───────────────┬─────────────────────────────────────────────┘
                │ samples in/out
                ▼
        Audio + MIDI hardware (via JUCE device manager)
```

**Layer by layer:**
- **Audio Engine** — Provided by Tracktion's `graph` module. A graph of processing nodes runs across multiple threads in a lock-free way. You mostly *configure* this rather than write it. Plugin Delay Compensation (keeping everything time-aligned when plugins add latency) lives here and is handled for you.
- **Model ("the Edit")** — Tracktion's core abstraction. Your project *is* an Edit: tracks, clips, the plugins on each track, automation curves, tempo map, markers. It's stored as a tree structure (JUCE's `ValueTree`), which is what gives you robust **undo/redo and save/load almost for free**.
- **Services** — The non-real-time workers: audio/MIDI device management, plugin scanning + hosting, file management for recorded audio, the render/export pipeline, and transport control. These run on background threads or the message thread — never on the audio thread.
- **UI** — JUCE Components. Each view *observes* the Edit and renders it; user actions become commands that mutate the Edit. The UI never touches the audio thread directly.

---
## 6. Threading model (the part that makes or breaks it)

| Thread | Priority | Does | Must never |
|---|---|---|---|
| **Audio thread** | Real-time (highest) | Runs the graph, processes each block of samples | Allocate memory, lock a mutex, block, or do file I/O |
| **Message thread** | Normal | GUI, drawing, user input | Do heavy work that stalls the UI |
| **Background threads** | Low | File read/write, plugin scanning, rendering, waveform thumbnails | — |

**The cardinal rule:** data crosses *into* the audio thread only through **lock-free structures** (lock-free FIFOs, atomic flags, message-passing). You never share a normal mutable object across that boundary.

**Tooling to enforce it:** Tracktion publishes `rtcheck`, a tool that catches real-time violations at runtime. **(See Addenda — `rtcheck` is macOS/Linux only, not Windows.)**

---
## 7. Signal flow

**Recording a track:**
```
Hardware in → device mgr → input node in graph
   ├─→ written to disk (background thread)
   └─→ new clip created in the Edit model
```

**Playing back:**
```
Edit model → graph is compiled → audio thread processes nodes:
   clip → track plugins → bus/sends → master → device mgr → speakers
```

**Making an edit (e.g., drag a clip, add a plugin):**
```
UI command → mutate Edit model (undoable) → graph rebuilt →
   next audio block reflects the change
```

Because the graph is rebuilt and swapped (not edited in place under the audio thread), edits happen without interrupting playback.

---
## 8. Plugin hosting (your headline requirement)
- **Scanning** — On demand (and a background re-scan), walk the standard VST3/AU folders. Validate each plugin **in a separate process** so a broken plugin can't crash your DAW. Cache the results. Tracktion's `pluginval` tool exists for exactly this out-of-process validation.
- **Loading** — Instantiate the plugin and wrap it as a node in the audio graph. JUCE handles the VST3/AU format details.
- **Hosting the plugin's UI** — Each plugin brings its own editor window; you host that inside a window in your app.
- **Crash isolation** — At minimum, scan/validate out-of-process. A later hardening step is running plugins themselves out-of-process.
- **Delay compensation** — Plugins add latency; the engine nudges everything back into alignment automatically.

---
## 9. Built-in instruments & effects
You ship a starter set so the DAW is usable before the user adds third-party plugins:
- **Effects:** EQ, compressor, delay, reverb, gain/utility. JUCE's DSP module gives you solid building blocks.
- **Instruments:** at least one synth and a basic sampler.

**Porting existing synth DSP:** any synth DSP you've already prototyped is portable. The *math* (oscillators, filters, envelopes) transfers directly to C++; only the wrapper changes — it becomes a JUCE/Tracktion instrument node fed by MIDI.

---
## 10. Project & repo structure

```
forge/
├── CMakeLists.txt              # top-level build
├── libs/
│   ├── JUCE/                   # (pulled by tracktion_engine submodule)
│   └── tracktion_engine/       # git submodule (pulls JUCE too)
├── src/
│   ├── main.cpp                # app entry, creates the Engine
│   ├── model/                  # your extensions to the Edit model
│   ├── engine/                 # engine setup, custom processing nodes
│   ├── services/
│   │   ├── devices/            # audio/MIDI device handling
│   │   ├── plugins/            # scan, host, cache
│   │   ├── files/              # project + recorded-audio management
│   │   └── render/             # export / stems
│   ├── ui/
│   │   ├── arrange/            # timeline + clips
│   │   ├── mixer/              # channel strips, buses, sends
│   │   ├── pianoroll/          # MIDI editing
│   │   ├── clipeditor/         # trim/fade/stretch
│   │   ├── transport/          # play/stop/record bar
│   │   └── browser/            # files + plugins
│   └── dsp/                    # your built-in effects + instruments
├── tests/
└── docs/
```

---
## 11. Build roadmap

| Phase | Goal | Definition of done |
|---|---|---|
| **0 — Toolchain** | Prove the build | JUCE + Tracktion Engine compile via CMake on Mac + Windows. App opens an empty window, opens the audio device, and plays a test sine. *Do not write features until this is green.* |
| **1 — The spine** | Record & play one track | Load/save a project (Edit). Create a track, import audio, play it through the transport with a moving playhead. Record one audio track in sync. |
| **2 — Mixer & plugins** | Make it capable | Mixer: volume/pan/mute/solo, buses, sends/returns. **Plugin scanning + VST3/AU hosting.** A couple of built-in effects. |
| **3 — MIDI & editing** | A DAW you'd use | MIDI tracks + piano roll (quantize, velocity). A built-in synth instrument. Non-destructive audio editing (trim/fade/stretch/pitch). Automation. |
| **4 — Polish** | Daily-driver feel | Comping across takes. Metering (levels + loudness). Export to WAV/MP3 + stems. Markers, snap-to-grid. |
| **5 — Deferred** | Pro extras, as wanted | Sidechaining, tempo warp, controller mapping, advanced routing, video sync. Only once 0–4 are solid. |

---
## 12. Risks & gotchas
- **Real-time discipline** — the #1 source of glitches. Any allocation/lock on the audio thread = audible problems.
- **Plugin crashes** — a third-party plugin can take down your whole DAW. Mitigate with out-of-process scanning (`pluginval`), and later out-of-process hosting.
- **Delay compensation** — forget it and your mix silently drifts out of time. The engine handles it; just don't override it.
- **Undo across subsystems** — lean entirely on Tracktion's `ValueTree`-based undo.
- **C++/build curve** — real, coming from Python/TS. Budget time for the toolchain (Phase 0 exists for this reason).
- **Cross-platform plugin formats** — VST3 is portable; AU is macOS-only.

---
## 13. Licensing (for "me + friends")
- **Tracktion Engine** — GPLv3 or commercial. The **GPLv3** path is free and fine for non-commercial use; if you hand the built app to friends you must make the source available to them too (trivial — share the repo).
- **JUCE** — has an open-source (AGPLv3) path and a free Starter tier. For non-commercial use you're covered.
- **Verify current terms** before you distribute. Revisit if "a few friends" turns into "selling it."

---
## Appendix A — The from-scratch alternative
If your goal is to **build the engine itself**, you drop Tracktion Engine and keep only JUCE. You take on: the lock-free multi-threaded audio graph, the project model + serialization + undo, the sample-accurate transport, and the plugin-host wrapping + delay compensation yourself. Scope delta: many months before reaching the *same* "capable" point. Choose it only if the engine *is* the project.

---
## Appendix B — Key references
- **Tracktion Engine** — github.com/Tracktion/tracktion_engine; tutorials & examples (PlaybackDemo). Docs at tracktion.github.io/tracktion_engine.
- **JUCE** — juce.com, module reference.
- **pluginval** — Tracktion's out-of-process plugin validator.
- **rtcheck** — Tracktion's real-time-safety checker (macOS/Linux).
- **JUCE Forum (Tracktion Engine category)** — the support channel.

---
## Addenda — Phase 0 research corrections (2026-06)
1. **`rtcheck` does not support Windows.** It is macOS-first, Linux WIP. On the Windows dev
   machine, real-time-safety verification relies on discipline + code review; run `rtcheck`
   only if/when Forge is also built on a Mac. (`pluginval` is unaffected — real and current.)
2. **ASIO is not free out of the box** — it needs the Steinberg ASIO SDK and `JUCE_ASIO=1`.
   Phase 0 (and early phases) use **WASAPI**, which works with no extra SDK; add ASIO when
   low input-monitoring latency becomes the priority.
3. **Licensing refined:** the clean open-source path is **JUCE under AGPLv3 + Tracktion
   under GPLv3 ⇒ Forge is AGPLv3** — free, no splash screen, no revenue caps. (Tracktion's
   free "Personal" *commercial* tier instead requires a "Powered by Tracktion Engine"
   splash; the GPL path avoids that.)
4. **Toolchain pinned:** C++20 is mandatory (Tracktion), so **VS2022 / MSVC v143** is
   required (VS2019 / v142 is inadequate). Tracktion pinned to **v3.2.0**; it bundles JUCE
   as a submodule.
