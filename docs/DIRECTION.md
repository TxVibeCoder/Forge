# Forge — Product Direction

> **Authoritative product brief. Written 2026-06-30 after a direction reset.**
> This supersedes the "arrangement-first" framing in earlier docs. Where [ARCHITECTURE.md](ARCHITECTURE.md),
> [INTERFACE.md](INTERFACE.md), or [STATUS.md](STATUS.md) still read linear-first, **this document wins**;
> they are being realigned to match it.

## One line

**Forge is a sample / scene-based DAW — an Ableton-style Session clip grid you play from a hardware grid
controller.** The linear arrangement timeline is a *secondary* view, not the primary surface.

## What Forge is (and isn't)

- **IS:** a clip-launching instrument. The primary surface is a **Session grid** — tracks across, **scenes**
  down — where every cell is a launchable clip. You build and perform music by triggering clips and scenes,
  live and quantized.
- **IS:** **controller-driven.** Forge is meant to be *played* from the class of Ableton-style performance
  controllers, with the on-screen grid and the hardware mirroring each other.
- **IS NOT (primarily):** a linear, record-into-a-timeline DAW. The Arrange timeline still exists (compose in
  Session, then arrange / bounce linearly) but it is the **secondary** view behind the Session grid.

> **History:** Forge was built arrangement-first through ~10 waves (Phases 0–4 + a MIDI MVP + piano-roll
> polish). That work is **not wasted** — clips, the 4OSC instrument, the piano-roll editor, the mixer, and
> plugin hosting are all building blocks that now live *inside* slots and scenes. What changes is the
> **primary surface and identity**, and the addition of a **control-surface layer**.

## The build pillars (and their order)

1. **Session-grid UI — the near-term build.** A new `SessionView` (tracks × scenes of clip slots) as the
   primary `ViewMode`, with launch / queue / stop states, scene launching, and per-slot clip editing (reusing
   the piano-roll). **Fully usable with mouse + keyboard — no hardware required.**
2. **Control-surface + MIDI-input layer — the goal it's designed toward ("one day").** Integration with
   **external physical hardware**: drivers that talk MIDI to/from *real* grid controllers, plus melodic note
   input, MIDI-learn, and clock/Link sync.

> **The controllers are real-world hardware** (a Launchpad, an APC40 on your desk) that Forge *connects to
> over MIDI* — **Forge does NOT render an on-screen controller.** The only on-screen surface is the
> `SessionView` grid. Build the grid first, **designed around the same pad-colour/state model the hardware
> will use**, so connecting a controller later is "add a driver," not a rework. Hardware integration is a
> hoped-for future capability, **not a gate on the MVP**.

## Primary surface — the Session clip grid

- **Model:** columns = tracks, rows = **scenes**; each cell = a **clip slot** (empty, has-clip, playing,
  queued). A scene fires a whole row. Launch quantization aligns triggers to the bar/beat.
- **Engine seam (verified):** Tracktion has first-class clip-launch — `AudioTrack::getClipSlotList()` →
  `ClipSlot`s, `Edit` scenes, `LaunchHandle`, launch quantization, follow actions (see the engine's
  `ClipLauncherDemo`). The clip-create path is the `te::insertMIDIClip(ClipSlot&, …)` / `insertNewClip` on a
  slot — distinct from the linear `AudioTrack`-member insert used in the MIDI MVP.
- **Mockup:** `mockups/00-session-clip-grid-primary.dxf` (first pass).

## Control surfaces — external hardware Forge connects to (not an on-screen surface)

These are **real, physical controllers on your desk** — Forge integrates with them over **MIDI**; it never
draws a controller on screen (the only on-screen surface is the `SessionView` grid). Forge supports the
**class** of Ableton-style performance controllers via a **device-agnostic layer** with a **driver per
controller**. Adding a controller = adding a driver, never a rearchitecture. *This is a "hope to one day
connect" capability — design for it now, build it when ready; the grid is fully playable without any
hardware. The `mockups/09` sheet is a **hardware-mapping reference**, not an app screen to build.*

- **Reference devices:**
  - **Novation Launchpad** — 8×8 RGB pad grid + arrow nav + scene-launch column + mode row. Uses the
    pad/navigation subset.
  - **Akai APC40 mkII** — 8×5 clip grid **plus** 8 + master faders, encoder banks (LED rings), transport,
    track mute/solo/rec buttons, crossfader. Uses the full surface.
- **Engine seam (verified, `tracktion_ControlSurface.h`):** one framework spans the whole class —
  - *Grid:* `userLaunchedClip(channel, scene, press)`, `userLaunchedScene`, `userStoppedClip`,
    `userScrolledTracks*`, `userChangedPadBanks` ↔ **LED feedback** `padStateChanged(channel, scene,
    colourIdx, state)`, `clipsPlayingStateChanged`; `showingClipSlots()`, `getClipSlotOffset()`,
    `numberOfTrackPads`, `limitedPadColours`.
  - *Faders / encoders:* `userMovedFader` / `moveFader` / `userMovedMasterLevelFader`, `userMovedPanPot`,
    `userMovedAux` (sends), `userMovedQuickParam`, `parameterChanged`.
  - *Buttons / transport / metering:* `userPressedMute/Solo/RecEnable`, `userPressedPlay/Record/Stop/…`,
    `channelLevelChanged`.
- **What ships:** only older Novation RemoteSL/Automap + Mackie-class drivers — **no grid driver**. We build
  Launchpad and APC40 drivers on this seam (JUCE `MidiInput`/`MidiOutput` translating device MIDI ↔ the API).

## MIDI input — all four roles

1. **Control-surface input** — the grid/fader/knob/transport control above.
2. **Melodic note input** — MIDI **keyboards / pads** play the per-track instrument (4OSC / sampler) and
   **record notes into clips** (the W7 MIDI-input-record path; needs its own enable sequence + a physical
   controller test — see [devlog/midi-design.md §5](devlog/midi-design.md)).
3. **MIDI-learn / parameter mapping** — bind any controller CC to track / plugin parameters
   (engine `tracktion_MidiLearn.h`).
4. **Clock / sync** — **MIDI clock** + **Ableton Link** (engine `tracktion_AbletonLink.{h,cpp}`,
   `MidiOutputDevice` clock) so Forge's scene-launch tempo syncs with other gear.

## What's reused from the arrangement-first work

Clips (audio + MIDI), the **4OSC** instrument seam + `ensureDefaultInstrument`, the **piano-roll** editor
(velocity / multi-select / copy-paste), the **mixer** (strips / inserts / meters / master), **plugin
hosting**, project save/load, export, `ForgeLookAndFeel`. The **Arrange** timeline becomes the secondary
view (compose in Session → arrange / bounce linearly).

## Roadmap (reset)

| Pillar | Work |
|---|---|
| **Session UI** | `SessionView` grid (slots/scenes, launch/queue/stop states) as the primary `ViewMode`; scene management; per-slot clip editing via the piano-roll; the `ViewMode` switch becomes `Session ∣ Arrange ∣ Mix` (Session default). |
| **Control surfaces** *(later — "one day")* | Integration with **external hardware** on the `ControlSurface` seam — **Launchpad** driver, then **APC40 mkII** driver; shared pad-colour/state model with the on-screen grid. The grid is fully usable without it. |
| **MIDI input** | Note input + record-into-clip (W7); MIDI-learn param mapping; MIDI-clock + Ableton Link sync. |
| **Vision artifacts** | Reflow the mockup set Session-first — DONE: Session is the primary sheet (currently **sheet 00**; a further 00→01 renumber is still pending), Arrange demoted, plus a controller-mapping sheet (Launchpad + APC40 over the grid, with the pad-colour legend). ARCHITECTURE/INTERFACE/STATUS realigned. |

> Open design questions to resolve before building: the fate of the control-bar **"Editor"** button (third
> view vs. drawer); whether the sampler/finger-drum workflow is its own wave; scene follow-actions scope.
